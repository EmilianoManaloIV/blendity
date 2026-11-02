// SPDX-License-Identifier: GPL-2.0-or-later
// Vector shapes and SVG for sketches (round 390, task 0367) - Illustrator's: the Rectangle, Ellipse, Polygon and Star
// as outlines, the Pen's Bezier paths sampled, and a sketch written as SVG and read back (path, rect, circle, ellipse,
// line, polyline and polygon; fill, stroke, stroke-width and the opacities, in attributes or style=; transforms on
// elements and groups). The canvas's frame is in view heights; SVG's user units are a thousandth of one, so a
// stroke's width in pixels is its stroke-width at a 1000-pixel view.
#include "sketch.h"

#include "../core/xml.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bl {

namespace {

constexpr float kSvgPi = 3.14159265358979f;
constexpr int kEllipseSteps = 64;       // (an ellipse's or a circle's points)
constexpr int kBezSteps = 16;           // (a cubic's points, its end included)
constexpr int kArcSteps = 16;           // (an arc's points per quarter turn)
constexpr float kStarInner = 0.5f;      // (Illustrator's Star: radius 2 half radius 1)
constexpr float kUnits = 1000.0f;       // (SVG user units in a view height)
constexpr float kForeignFit = 0.8f;     // (another program's drawing: its height fitted to this much of the view)
constexpr float kForeignAt = 0.1f;      // (its top-left corner here)
constexpr size_t kMaxPoints = 4000000;  // (a file's points at most: past this the rest is left out)
constexpr float kMaxWidth = 200.0f;     // (a line's width at most, in pixels: the pen's own most)
constexpr float kMaxCoord = 1.0e6f;     // (re-check 390: a point further out than this many view heights leaves its shape out - drawn, it overflows)

/* A 2D affine map: x' = a x + c y + e, y' = b x + d y + f (SVG's matrix(a b c d e f)). */
struct Affine {
  float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, e = 0.0f, f = 0.0f;
  Vec2 map(Vec2 p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
  float scale() const { return std::sqrt(std::fabs(a * d - b * c)); }
};

/* m after n: a point goes through n, then m. */
Affine operator*(const Affine &m, const Affine &n) {
  Affine r;
  r.a = m.a * n.a + m.c * n.b, r.b = m.b * n.a + m.d * n.b;
  r.c = m.a * n.c + m.c * n.d, r.d = m.b * n.c + m.d * n.d;
  r.e = m.a * n.e + m.c * n.f + m.e, r.f = m.b * n.e + m.d * n.f + m.f;
  return r;
}

/* Numbers in an attribute, SVG's way: commas or spaces between, "1.5.5" is 1.5 and .5, "-1-2" is -1 and -2. */
struct Numbers {
  const char *p, *e;
  explicit Numbers(const std::string &s) : p(s.c_str()), e(s.c_str() + s.size()) {}
  void skip() {
    while (p < e && (std::isspace((unsigned char)*p) || *p == ',')) p++;
  }
  bool more() {
    skip();
    return p < e && (std::isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.');
  }
  bool next(float &v) {
    if (!more()) return false;
    char *end = nullptr;
    v = std::strtof(p, &end);
    if (end == p) return false;
    p = end;
    return std::isfinite(v);
  }
  /* An arc's flag: one digit, "0" or "1", which may run into the next number ("a1 1 0 01 2 3"). */
  bool flag(bool &v) {
    skip();
    if (p >= e || (*p != '0' && *p != '1')) return false;
    v = *p++ == '1';
    return true;
  }
};

std::string fmt(float v) {
  char b[64];  // (review 390: room for a large coordinate's every digit)
  std::snprintf(b, sizeof(b), "%.3f", v);
  std::string s = b;
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s == "-0" ? "0" : s;
}

std::string hex_rgb(uint32_t c) {
  char b[8];
  std::snprintf(b, sizeof(b), "#%06x", c & 0xFFFFFF);
  return b;
}

std::string alpha_text(uint32_t c) { return fmt(((c >> 24) & 0xFF) / 255.0f); }

/* --- writing --- */

std::string path_of_points(const std::vector<SketchPoint> &pts, bool closed) {
  std::string d;
  for (size_t i = 0; i < pts.size(); i++) d += (i ? " L" : "M") + fmt(pts[i].x * kUnits) + " " + fmt(pts[i].y * kUnits);
  return closed ? d + " Z" : d;
}

std::string pt_text(Vec2 p) { return fmt(p.x * kUnits) + " " + fmt(p.y * kUnits); }

std::string path_of_bez(const std::vector<Vec2> &bez, bool closed) {
  const size_t n = bez.size() / 3;
  std::string d = "M" + pt_text(bez[0]);
  const size_t segs = closed ? n : n - 1;
  for (size_t i = 0; i < segs; i++) {
    const size_t j = (i + 1) % n;
    d += " C" + pt_text(bez[3 * i + 2]) + " " + pt_text(bez[3 * j + 1]) + " " + pt_text(bez[3 * j]);
  }
  return closed ? d + " Z" : d;
}

std::string stroke_element(const SketchStroke &st) {
  const bool bez = st.bez.size() >= 6 && st.bez.size() % 3 == 0;
  const std::string d = bez ? path_of_bez(st.bez, st.closed) : path_of_points(st.pts, st.closed);
  const uint32_t fill = st.filled ? st.color : st.fill;
  const bool line = !st.filled && st.width > 0.0f;
  std::string e = "<path d=\"" + d + "\"";
  e += fill ? " fill=\"" + hex_rgb(fill) + "\" fill-opacity=\"" + alpha_text(fill) + "\"" : std::string(" fill=\"none\"");
  if (line) e += " stroke=\"" + hex_rgb(st.color) + "\" stroke-opacity=\"" + alpha_text(st.color) + "\" stroke-width=\"" + fmt(st.width) + "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"";
  if (st.opacity < 1.0f) e += " opacity=\"" + fmt(st.opacity) + "\"";
  return e + "/>\n";
}

std::string css_blend(int mode) {
  static const int kCss[] = {kSketchMultiply, kSketchScreen, kSketchOverlay, kSketchDarken, kSketchLighten, kSketchColorDodge, kSketchColorBurn, kSketchHardLight,
                             kSketchSoftLight, kSketchDifference, kSketchExclusion, kSketchHue, kSketchSaturation, kSketchColor, kSketchLuminosity};
  if (std::find(std::begin(kCss), std::end(kCss), mode) == std::end(kCss)) return "";  // (no CSS name: Normal in the file)
  std::string s;
  for (const char *p = sketch_blend_name(mode); *p; p++) s += *p == ' ' ? '-' : (char)std::tolower((unsigned char)*p);
  return s;
}

std::string xml_text(const std::string &s) {
  std::string o;
  for (char ch : s) o += ch == '&' ? "&amp;" : ch == '<' ? "&lt;" : ch == '>' ? "&gt;" : ch == '"' ? "&quot;" : std::string(1, ch);
  return o;
}

/* --- reading --- */

struct Style {
  uint32_t fill = 0xFF000000;  // (SVG's default fill: black)
  uint32_t stroke = 0;         // (none)
  float width = 1.0f, opacity = 1.0f, fill_opacity = 1.0f, stroke_opacity = 1.0f;
  float own_opacity = 1.0f;  // (this element's own opacity: style= wins over the attribute, then it multiplies in once)
};

int hex_digit(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  ch = (char)std::tolower((unsigned char)ch);
  return ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
}

/* A colour: #rgb, #rrggbb, rgb(r, g, b) or a few names; `ok` false for one it doesn't know (the attribute is then left
 * as inherited). 0 is none. */
uint32_t parse_color(std::string v, bool &ok) {
  ok = true;
  while (!v.empty() && std::isspace((unsigned char)v.back())) v.pop_back();
  while (!v.empty() && std::isspace((unsigned char)v.front())) v.erase(v.begin());
  for (char &ch : v) ch = (char)std::tolower((unsigned char)ch);
  if (v == "none" || v == "transparent") return 0;
  if (v.rfind("url(", 0) == 0) {  // (review 390: a gradient or pattern - its fallback colour, else none; it had come in black)
    const size_t close = v.find(')');
    bool fallback = false;
    const uint32_t c = close == std::string::npos ? 0 : parse_color(v.substr(close + 1), fallback);
    return fallback ? c : 0;
  }
  if (v.size() == 4 && v[0] == '#') {
    uint32_t c = 0;
    for (int i = 1; i < 4; i++) c = (c << 8) | (uint32_t)(std::max(0, hex_digit(v[i])) * 17);
    return 0xFF000000u | c;
  }
  if (v.size() == 7 && v[0] == '#') return 0xFF000000u | (uint32_t)std::strtoul(v.c_str() + 1, nullptr, 16);
  if (v.rfind("rgb(", 0) == 0) {
    const std::string args = v.substr(4);  // (kept: Numbers points into it)
    Numbers n(args);
    float r = 0, g = 0, b = 0;
    n.next(r), n.next(g), n.next(b);
    auto ch = [](float x) { return (uint32_t)std::clamp((int)std::lround(x), 0, 255); };
    return 0xFF000000u | ch(r) << 16 | ch(g) << 8 | ch(b);
  }
  static const std::pair<const char *, uint32_t> kNames[] = {{"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000}, {"green", 0x008000},
                                                              {"blue", 0x0000FF}, {"yellow", 0xFFFF00}, {"gray", 0x808080}, {"grey", 0x808080},
                                                              {"orange", 0xFFA500}, {"purple", 0x800080}, {"currentcolor", 0x000000}};
  for (const auto &n : kNames)
    if (v == n.first) return 0xFF000000u | n.second;
  ok = false;
  return 0;
}

float parse_float(const std::string &v, float d) {
  Numbers n(v);
  float x = d;
  return n.next(x) ? x : d;
}

void apply_property(Style &s, const std::string &k, const std::string &v) {
  bool ok = false;
  if (k == "fill") {
    const uint32_t c = parse_color(v, ok);
    if (ok) s.fill = c;
  }
  else if (k == "stroke") {
    const uint32_t c = parse_color(v, ok);
    if (ok) s.stroke = c;
  }
  else if (k == "stroke-width") s.width = std::max(0.0f, parse_float(v, s.width));
  else if (k == "opacity") s.own_opacity = std::clamp(parse_float(v, 1.0f), 0.0f, 1.0f);
  else if (k == "fill-opacity") s.fill_opacity = std::clamp(parse_float(v, 1.0f), 0.0f, 1.0f);
  else if (k == "stroke-opacity") s.stroke_opacity = std::clamp(parse_float(v, 1.0f), 0.0f, 1.0f);
}

std::string trimmed(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace((unsigned char)s[a])) a++;
  while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
  return s.substr(a, b - a);
}

/* The element's own style over what it inherits: the attributes, then style= (which wins, as in CSS). */
Style style_of(const Xml &x, Style s) {
  s.own_opacity = 1.0f;
  static const char *const kKeys[] = {"fill", "stroke", "stroke-width", "opacity", "fill-opacity", "stroke-opacity"};
  for (const char *k : kKeys)
    if (!x.attr(k).empty()) apply_property(s, k, x.attr(k));
  const std::string &style = x.attr("style");
  size_t at = 0;
  while (at < style.size()) {
    size_t semi = style.find(';', at);
    if (semi == std::string::npos) semi = style.size();
    const std::string decl = style.substr(at, semi - at);
    const size_t colon = decl.find(':');
    if (colon != std::string::npos) apply_property(s, trimmed(decl.substr(0, colon)), trimmed(decl.substr(colon + 1)));
    at = semi + 1;
  }
  s.opacity *= s.own_opacity;  // (review 390: once - set both ways it had counted twice)
  return s;
}

/* transform="translate(..) scale(..) rotate(a [cx cy]) skewX(..) skewY(..) matrix(..)", applied left to right. */
Affine parse_transform(const std::string &t) {
  Affine m;
  size_t at = 0;
  while (at < t.size()) {
    const size_t open = t.find('(', at), close = t.find(')', at);
    if (open == std::string::npos || close == std::string::npos || close < open) break;
    const std::string name = trimmed(t.substr(at, open - at));
    std::vector<float> v;
    const std::string args = t.substr(open + 1, close - open - 1);  // (kept: Numbers points into it)
    Numbers n(args);
    for (float x; n.next(x);) v.push_back(x);
    at = close + 1;
    while (at < t.size() && (std::isspace((unsigned char)t[at]) || t[at] == ',')) at++;
    Affine k;
    const std::string &nm = name;
    if (nm == "matrix" && v.size() == 6) k = {v[0], v[1], v[2], v[3], v[4], v[5]};
    else if (nm == "translate" && !v.empty()) k.e = v[0], k.f = v.size() > 1 ? v[1] : 0.0f;
    else if (nm == "scale" && !v.empty()) k.a = v[0], k.d = v.size() > 1 ? v[1] : v[0];
    else if (nm == "rotate" && !v.empty()) {
      const float r = v[0] * kSvgPi / 180.0f, cx = v.size() > 2 ? v[1] : 0.0f, cy = v.size() > 2 ? v[2] : 0.0f;
      const Affine to{1, 0, 0, 1, cx, cy}, back{1, 0, 0, 1, -cx, -cy};
      k = to * Affine{std::cos(r), std::sin(r), -std::sin(r), std::cos(r), 0, 0} * back;
    }
    else if (nm == "skewX" && !v.empty()) k.c = std::tan(v[0] * kSvgPi / 180.0f);
    else if (nm == "skewY" && !v.empty()) k.b = std::tan(v[0] * kSvgPi / 180.0f);
    m = m * k;
  }
  return m;
}

/* One subpath as it is read: its points (lines as their ends, curves sampled) and, while it holds no arc, its anchors
 * (anchor, in, out each). */
struct Sub {
  std::vector<Vec2> pts, bez;
  bool closed = false, curved = false, arc = false;
};

void cubic_to(Sub &s, Vec2 p0, Vec2 c1, Vec2 c2, Vec2 p3) {
  for (int k = 1; k <= kBezSteps; k++) {
    const float t = k / (float)kBezSteps, u = 1.0f - t;
    s.pts.push_back(p0 * (u * u * u) + c1 * (3.0f * u * u * t) + c2 * (3.0f * u * t * t) + p3 * (t * t * t));
  }
  if (s.bez.size() >= 3) s.bez[s.bez.size() - 1] = c1;
  s.bez.insert(s.bez.end(), {p3, c2, p3});
  s.curved = true;
}

void line_to(Sub &s, Vec2 p) {
  s.pts.push_back(p);
  s.bez.insert(s.bez.end(), {p, p, p});
}

/* SVG's endpoint arc (rx ry rotation large sweep x y) sampled - the W3C's conversion to its centre (SVG 1.1 F.6.5). */
void arc_to(Sub &s, Vec2 p0, float rx, float ry, float rot_deg, bool large, bool sweep, Vec2 p1) {
  s.arc = true;
  rx = std::fabs(rx), ry = std::fabs(ry);
  if (rx < 1e-9f || ry < 1e-9f || length(p1 - p0) < 1e-9f) return line_to(s, p1);
  const float phi = rot_deg * kSvgPi / 180.0f, cp = std::cos(phi), sp = std::sin(phi);
  const Vec2 h = (p0 - p1) * 0.5f;
  const float x1 = cp * h.x + sp * h.y, y1 = -sp * h.x + cp * h.y;
  const float lambda = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
  if (lambda > 1.0f) rx *= std::sqrt(lambda), ry *= std::sqrt(lambda);
  const float num = std::max(0.0f, rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1);
  const float den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
  const float k = (large == sweep ? -1.0f : 1.0f) * std::sqrt(den > 0.0f ? num / den : 0.0f);
  const float cx1 = k * rx * y1 / ry, cy1 = -k * ry * x1 / rx;
  const Vec2 c{cp * cx1 - sp * cy1 + (p0.x + p1.x) * 0.5f, sp * cx1 + cp * cy1 + (p0.y + p1.y) * 0.5f};
  const float t0 = std::atan2((y1 - cy1) / ry, (x1 - cx1) / rx);
  float dt = std::atan2((-y1 - cy1) / ry, (-x1 - cx1) / rx) - t0;
  if (sweep && dt < 0.0f) dt += 2.0f * kSvgPi;
  if (!sweep && dt > 0.0f) dt -= 2.0f * kSvgPi;
  const int n = std::max(2, (int)std::ceil(std::fabs(dt) / (kSvgPi * 0.5f) * kArcSteps));
  for (int i = 1; i <= n; i++) {
    const float t = t0 + dt * i / (float)n;
    s.pts.push_back({c.x + rx * std::cos(t) * cp - ry * std::sin(t) * sp, c.y + rx * std::cos(t) * sp + ry * std::sin(t) * cp});
  }
  s.pts.back() = p1;
}

void close_sub(Sub &s) {
  if (s.closed) return;  // (review 390: a second Z had cut another anchor off)
  s.closed = true;
  if (s.pts.size() >= 2 && length(s.pts.back() - s.pts.front()) > 1e-6f) line_to(s, s.pts.front());
  if (s.bez.size() < 6) return;
  s.bez[1] = s.bez[s.bez.size() - 2];  // (the last anchor is the first again: its in-handle is the first's)
  s.bez.resize(s.bez.size() - 3);
}

/* A path's d= into its subpaths. */
std::vector<Sub> parse_path(const std::string &d) {
  std::vector<Sub> out;
  Numbers n(d);
  Vec2 cur, start, last_c2, last_q;
  char cmd = 0, prev = 0;
  for (;;) {
    n.skip();
    if (n.p >= n.e) break;
    if (std::isalpha((unsigned char)*n.p)) cmd = *n.p++;
    else if (!cmd) break;
    const bool rel = std::islower((unsigned char)cmd) != 0;
    const char c = (char)std::toupper((unsigned char)cmd);
    const Vec2 o = rel ? cur : Vec2{0.0f, 0.0f};
    float v[7];
    auto read = [&](int k) {
      for (int i = 0; i < k; i++)
        if (!n.next(v[i])) return false;
      return true;
    };
    if (c == 'Z') {
      if (!out.empty()) close_sub(out.back());
      cur = start, prev = 'Z', cmd = 0;  // (review 390: numbers after Z have no command - without this they looped for ever)
      continue;
    }
    if (c == 'M') {
      if (!read(2)) break;
      cur = start = o + Vec2{v[0], v[1]};
      out.push_back({});
      out.back().pts.push_back(cur), out.back().bez = {cur, cur, cur};
      cmd = rel ? 'l' : 'L', prev = 'M';  // (more pairs after a moveto are linetos)
      continue;
    }
    if (out.empty() || prev == 'Z') out.push_back(Sub{{cur}, {cur, cur, cur}});  // (a command after Z draws on from the subpath's start)
    Sub &s = out.back();
    if (c == 'L' && read(2)) line_to(s, cur = o + Vec2{v[0], v[1]});
    else if (c == 'H' && read(1)) line_to(s, cur = {rel ? cur.x + v[0] : v[0], cur.y});
    else if (c == 'V' && read(1)) line_to(s, cur = {cur.x, rel ? cur.y + v[0] : v[0]});
    else if (c == 'C' && read(6)) {
      const Vec2 c1 = o + Vec2{v[0], v[1]}, c2 = o + Vec2{v[2], v[3]}, p = o + Vec2{v[4], v[5]};
      cubic_to(s, cur, c1, c2, p), last_c2 = c2, cur = p;
    }
    else if (c == 'S' && read(4)) {
      const Vec2 c1 = (prev == 'C' || prev == 'S') ? cur * 2.0f - last_c2 : cur, c2 = o + Vec2{v[0], v[1]}, p = o + Vec2{v[2], v[3]};
      cubic_to(s, cur, c1, c2, p), last_c2 = c2, cur = p;
    }
    else if (c == 'Q' && read(4)) {
      const Vec2 q = o + Vec2{v[0], v[1]}, p = o + Vec2{v[2], v[3]};
      cubic_to(s, cur, cur + (q - cur) * (2.0f / 3.0f), p + (q - p) * (2.0f / 3.0f), p), last_q = q, cur = p;
    }
    else if (c == 'T' && read(2)) {
      const Vec2 q = (prev == 'Q' || prev == 'T') ? cur * 2.0f - last_q : cur, p = o + Vec2{v[0], v[1]};
      cubic_to(s, cur, cur + (q - cur) * (2.0f / 3.0f), p + (q - p) * (2.0f / 3.0f), p), last_q = q, cur = p;
    }
    else if (c == 'A') {
      bool large = false, sweep = false;
      if (!(n.next(v[0]) && n.next(v[1]) && n.next(v[2]) && n.flag(large) && n.flag(sweep) && n.next(v[5]) && n.next(v[6]))) break;
      const Vec2 p = o + Vec2{v[5], v[6]};
      arc_to(s, cur, v[0], v[1], v[2], large, sweep, p), cur = p;
    }
    else break;  // (a command it can't read, or numbers missing: the rest is left out)
    prev = c;
  }
  return out;
}

std::vector<Vec2> numbers_as_points(const std::string &s) {
  std::vector<Vec2> pts;
  Numbers n(s);
  for (float x, y; n.next(x) && n.next(y);) pts.push_back({x, y});
  return pts;
}

float attr_f(const Xml &x, const char *k) { return parse_float(x.attr(k), 0.0f); }

/* An element's shape as subpaths, in its own user units. */
std::vector<Sub> shape_subs(const Xml &x) {
  const std::string &k = x.name;
  if (k == "path") return parse_path(x.attr("d"));
  Sub s;
  if (k == "rect") {
    const float px = attr_f(x, "x"), py = attr_f(x, "y"), w = attr_f(x, "width"), h = attr_f(x, "height");
    if (w <= 0.0f || h <= 0.0f) return {};
    for (Vec2 p : sketch_shape_outline(1, 4, {px, py}, {px + w, py + h})) s.pts.push_back(p);
    s.closed = true;
  }
  else if (k == "circle" || k == "ellipse") {
    const float r = attr_f(x, "r"), rx = k == "circle" ? r : attr_f(x, "rx"), ry = k == "circle" ? r : attr_f(x, "ry");
    const Vec2 c{attr_f(x, "cx"), attr_f(x, "cy")};
    if (rx <= 0.0f || ry <= 0.0f) return {};
    s.pts = sketch_shape_outline(2, 0, c - Vec2{rx, ry}, c + Vec2{rx, ry});
    s.closed = true;
  }
  else if (k == "line") s.pts = {{attr_f(x, "x1"), attr_f(x, "y1")}, {attr_f(x, "x2"), attr_f(x, "y2")}};
  else if (k == "polyline" || k == "polygon") {
    s.pts = numbers_as_points(x.attr("points"));
    if (k == "polygon" && s.pts.size() >= 2) s.closed = true, s.pts.push_back(s.pts.front());
  }
  if (s.pts.size() < 2) return {};
  return {s};
}

struct Reader {
  Affine to_canvas;  // (user units of the root into the canvas's frame)
  float width_scale = 1.0f;  // (a canvas-frame length into pixels at a 1000-pixel view)
  std::vector<SketchSvgLayer> *layers = nullptr;
  size_t points = 0;
  bool ours = false;

  void stroke_of(const Sub &sub, const Style &st, const Affine &m) {
    const uint32_t fill_a = (uint32_t)std::lround(((st.fill >> 24) & 0xFF) * st.fill_opacity * st.opacity);
    const uint32_t line_a = (uint32_t)std::lround(((st.stroke >> 24) & 0xFF) * st.stroke_opacity * st.opacity);
    const bool fill = st.fill && fill_a > 0 && sub.pts.size() >= 3;
    const bool line = st.stroke && line_a > 0 && st.width > 0.0f;
    if ((!fill && !line) || points + sub.pts.size() > kMaxPoints) return;
    points += sub.pts.size();
    const Affine all = to_canvas * m;
    SketchStroke out;
    out.closed = sub.closed;
    out.fill = fill ? (fill_a << 24) | (st.fill & 0xFFFFFF) : 0;
    out.color = line ? (line_a << 24) | (st.stroke & 0xFFFFFF) : out.fill;
    out.width = line ? st.width * m.scale() * width_scale : 0.0f;
    const bool keep_bez = sub.curved && !sub.arc && sub.bez.size() >= 6;
    for (Vec2 p : keep_bez ? std::vector<Vec2>() : sub.pts) out.pts.push_back({all.map(p).x, all.map(p).y, 1.0f});
    for (Vec2 p : keep_bez ? sub.bez : std::vector<Vec2>()) out.bez.push_back(all.map(p));
    if (keep_bez)
      for (Vec2 p : sketch_bezier_points(out.bez, out.closed)) out.pts.push_back({p.x, p.y, 1.0f});
    auto sane = [](float v) { return std::isfinite(v) && std::fabs(v) <= kMaxCoord; };
    const bool finite = std::all_of(out.pts.begin(), out.pts.end(), [&](const SketchPoint &p) { return sane(p.x) && sane(p.y); });
    if (!finite || !std::isfinite(out.width)) return;  // (review 390: a transform or view box that overflows: the shape left out)
    out.width = std::min(out.width, kMaxWidth);
    layers->back().strokes.push_back(std::move(out));
  }

  void element(const Xml &x, Style st, Affine m) {
    static const char *const kSkip[] = {"defs", "clipPath", "mask", "symbol", "style", "title", "desc", "metadata", "pattern", "marker", "linearGradient", "radialGradient"};
    for (const char *k : kSkip)
      if (x.name == k) return;
    st = style_of(x, st);
    m = m * parse_transform(x.attr("transform"));
    if (x.name == "g" && ours && !x.attr("data-layer").empty()) return layer_group(x, st, m);
    for (const Sub &sub : shape_subs(x)) stroke_of(sub, st, m);
    for (const Xml &c : x.children) element(c, st, m);
  }

  /* One of Blendity's own layers: a layer again, its opacity and blend its own (not the strokes'). */
  void layer_group(const Xml &x, Style st, const Affine &m) {
    SketchSvgLayer l;
    l.name = x.attr("data-layer");
    l.opacity = std::clamp(parse_float(x.attr("opacity"), 1.0f), 0.0f, 1.0f);
    const std::string &css = x.attr("style");
    const size_t at = css.find("mix-blend-mode:");
    l.blend = at == std::string::npos ? 0 : std::max(0, sketch_blend_index(trimmed(css.substr(at + 15, css.find(';', at) - at - 15))));
    layers->push_back(std::move(l));
    st.opacity = 1.0f;
    for (const Xml &c : x.children) element(c, st, m);
    layers->push_back({});  // (anything after the group goes to a layer of its own)
    layers->back().name = "SVG";
  }
};

}  // namespace

std::vector<Vec2> sketch_shape_outline(int kind, int sides, Vec2 a, Vec2 b) {
  const Vec2 lo{std::min(a.x, b.x), std::min(a.y, b.y)}, hi{std::max(a.x, b.x), std::max(a.y, b.y)};
  const Vec2 c = (lo + hi) * 0.5f, r = (hi - lo) * 0.5f;
  if (kind == 1) return {lo, {hi.x, lo.y}, hi, {lo.x, hi.y}, lo};
  const bool star = kind == 4;
  const int n = kind == 2 ? kEllipseSteps : std::clamp(sides, 3, 100) * (star ? 2 : 1);
  std::vector<Vec2> out;
  for (int i = 0; i <= n; i++) {
    const float t = (i % n) / (float)n * 2.0f * kSvgPi - (kind == 2 ? 0.0f : kSvgPi * 0.5f);  // (a polygon's and a star's first point straight up)
    const float k = star && (i % 2) ? kStarInner : 1.0f;
    out.push_back({c.x + std::cos(t) * r.x * k, c.y + std::sin(t) * r.y * k});
  }
  return out;
}

std::vector<Vec2> sketch_bezier_points(const std::vector<Vec2> &bez, bool closed) {
  const size_t n = bez.size() / 3;
  if (n == 0) return {};
  std::vector<Vec2> out{bez[0]};
  const size_t segs = closed ? n : n - 1;
  for (size_t i = 0; i < segs; i++) {
    const size_t j = (i + 1) % n;
    const Vec2 p0 = bez[3 * i], c1 = bez[3 * i + 2], c2 = bez[3 * j + 1], p3 = bez[3 * j];
    for (int k = 1; k <= kBezSteps; k++) {
      const float t = k / (float)kBezSteps, u = 1.0f - t;
      out.push_back(p0 * (u * u * u) + c1 * (3.0f * u * u * t) + c2 * (3.0f * u * t * t) + p3 * (t * t * t));
    }
  }
  return out;
}

std::string sketch_svg_text(const Sketch &s, Vec2 lo, Vec2 hi) {
  const Vec2 size = hi - lo;
  std::string o = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  o += "<svg xmlns=\"http://www.w3.org/2000/svg\" data-blendity=\"1\" viewBox=\"" + pt_text(lo) + " " + pt_text(size) + "\" width=\"" + fmt(size.x * kUnits) +
       "\" height=\"" + fmt(size.y * kUnits) + "\">\n";
  for (size_t li = 0; li < s.layers.size(); li++) {
    const SketchLayer &l = s.layers[li];
    if (!s.layer_shown((int)li) || l.is_group) continue;  // (review 390: a hidden group's layers too)
    const std::string blend = css_blend(l.blend);
    o += "<g data-layer=\"" + xml_text(l.name) + "\" opacity=\"" + fmt(l.opacity) + "\"" + (blend.empty() ? "" : " style=\"mix-blend-mode:" + blend + "\"") + ">\n";
    for (const SketchStroke &st : s.strokes)
      if (st.layer == (int)li && st.canvas == 0 && st.pts.size() >= 2) o += stroke_element(st);
    o += "</g>\n";
  }
  return o + "</svg>\n";
}

bool sketch_svg_read(const std::string &text, std::vector<SketchSvgLayer> &layers, std::string &err) {
  Xml root;
  XmlParser xp(text.data(), text.data() + text.size());
  if (!xp.document(root) || root.name != "svg") return err = "not an SVG file (no <svg> element)", false;
  Reader r;
  r.layers = &layers;
  r.ours = root.attr("data-blendity") == "1";
  std::vector<float> vb;
  Numbers n(root.attr("viewBox"));
  for (float x; n.next(x);) vb.push_back(x);
  if (vb.size() != 4 || vb[3] <= 0.0f) vb = {0.0f, 0.0f, parse_float(root.attr("width"), 0.0f), parse_float(root.attr("height"), 0.0f)};
  const float s = r.ours ? 1.0f / kUnits : vb[3] > 0.0f ? kForeignFit / vb[3] : 1.0f / kUnits;
  const Vec2 at = r.ours ? Vec2{0.0f, 0.0f} : Vec2{kForeignAt - vb[0] * s, kForeignAt - vb[1] * s};
  r.to_canvas = {s, 0.0f, 0.0f, s, at.x, at.y};
  r.width_scale = s * kUnits;
  layers.push_back({});
  layers.back().name = "SVG";
  r.element(root, Style{}, Affine{});
  layers.erase(std::remove_if(layers.begin(), layers.end(), [](const SketchSvgLayer &l) { return l.strokes.empty() && l.name == "SVG"; }), layers.end());
  if (r.points >= kMaxPoints) err = "the file's points past four million were left out";
  return true;
}

}  // namespace bl

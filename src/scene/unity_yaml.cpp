// SPDX-License-Identifier: GPL-2.0-or-later
// Unity's YAML (round 370, task 0347): see unity_yaml.h.
#include "unity_yaml.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace bl {

namespace {

struct Line {
  int indent = 0;
  std::string text;  // (without its indent and line end)
};

std::string trim(const std::string &s) {
  const size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::vector<Line> lines_of(const std::string &body) {
  std::vector<Line> out;
  for (size_t at = 0; at < body.size();) {
    size_t end = body.find('\n', at);
    if (end == std::string::npos) end = body.size();
    std::string raw = body.substr(at, end - at);
    at = end + 1;
    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
    const size_t first = raw.find_first_not_of(' ');
    if (first == std::string::npos || raw[first] == '#') continue;
    out.push_back({(int)first, raw.substr(first)});
  }
  return out;
}

/* A code point as UTF-8. */
void put_utf8(std::string &out, uint32_t cp) {
  if (cp < 0x80) out += (char)cp;
  else if (cp < 0x800) out += (char)(0xC0 | cp >> 6), out += (char)(0x80 | (cp & 63));
  else if (cp < 0x10000) out += (char)(0xE0 | cp >> 12), out += (char)(0x80 | (cp >> 6 & 63)), out += (char)(0x80 | (cp & 63));
  else out += (char)(0xF0 | cp >> 18), out += (char)(0x80 | (cp >> 12 & 63)), out += (char)(0x80 | (cp >> 6 & 63)), out += (char)(0x80 | (cp & 63));
}

/* `n` hex digits at s[i]; false when they aren't. */
bool hex_at(const std::string &s, size_t i, int n, uint32_t &v) {
  v = 0;
  for (int k = 0; k < n; k++) {
    if (i + k >= s.size() || !std::isxdigit((unsigned char)s[i + k])) return false;
    const char c = s[i + k];
    v = v * 16 + (uint32_t)(c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
  }
  return true;
}

/* A double-quoted escape at s[i] (past the backslash) into `out`; how many characters it took. Review 370: Unity
 * writes Japanese and other names as \uXXXX (surrogate pairs for the rest). */
size_t escape(const std::string &s, size_t i, std::string &out) {
  const char e = s[i];
  uint32_t cp = 0, lo = 0;
  if (e == 'u' && hex_at(s, i + 1, 4, cp)) {
    if (cp >= 0xD800 && cp < 0xDC00 && s.compare(i + 5, 2, "\\u") == 0 && hex_at(s, i + 7, 4, lo) && lo >= 0xDC00 && lo < 0xE000)
      return put_utf8(out, 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00)), 11;
    return put_utf8(out, cp >= 0xD800 && cp < 0xE000 ? 0xFFFD : cp), 5;
  }
  if (e == 'U' && hex_at(s, i + 1, 8, cp)) return put_utf8(out, cp <= 0x10FFFF ? cp : 0xFFFD), 9;
  if (e == 'x' && hex_at(s, i + 1, 2, cp)) return put_utf8(out, cp), 3;
  static const char kFrom[] = "ntr0be/ \"", kTo[] = {'\n', '\t', '\r', '\0', '\b', 27, '/', ' ', '"'};
  const char *at = std::strchr(kFrom, e);
  out += at && *at ? kTo[at - kFrom] : e;
  return 1;
}

/* A scalar's text: quotes taken off ('' inside single quotes; double ones' escapes, \uXXXX among them). */
std::string unquote(const std::string &in) {
  const std::string s = trim(in);
  if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
    std::string out;
    for (size_t i = 1; i + 1 < s.size(); i++) out += s[i], i += s[i] == '\'' && s[i + 1] == '\'';
    return out;
  }
  if (s.size() < 2 || s.front() != '"' || s.back() != '"') return s;
  std::string out;
  const std::string body = s.substr(1, s.size() - 2);
  for (size_t i = 0; i < body.size(); i++) {
    if (body[i] != '\\' || i + 1 >= body.size()) {
      out += body[i];
      continue;
    }
    i += escape(body, i + 1, out);
  }
  return out;
}

/* Where a key's colon is (": " or a colon ending the line), outside quotes and braces; npos when none. */
size_t key_colon(const std::string &t) {
  char quote = 0;
  int depth = 0;
  for (size_t i = 0; i < t.size(); i++) {
    const char c = t[i];
    if (quote) {
      quote = c == quote ? 0 : quote;
      continue;
    }
    if (c == '\'' || c == '"') quote = c;
    depth += c == '{' || c == '[' ? 1 : c == '}' || c == ']' ? -1 : 0;
    if (c == ':' && depth == 0 && (i + 1 == t.size() || t[i + 1] == ' ')) return i;
  }
  return std::string::npos;
}

YamlNode scalar(const std::string &s) {
  YamlNode n;
  const std::string t = trim(s);
  if (t.empty() || t == "~" || t == "null") return n;
  n.kind = YamlNode::Scalar;
  n.value = unquote(t);
  return n;
}

void skip_spaces(const std::string &s, size_t &at) {
  while (at < s.size() && (s[at] == ' ' || s[at] == '\t')) at++;
}

/* A flow mapping's key colon from `at`, outside quotes (review 370: {"a:b": 1}). */
size_t flow_colon(const std::string &s, size_t at) {
  char quote = 0;
  for (size_t i = at; i < s.size(); i++) {
    if (quote) quote = s[i] == quote ? 0 : quote;
    else if (s[i] == '\'' || s[i] == '"') quote = s[i];
    else if (s[i] == ':') return i;
  }
  return std::string::npos;
}

/* A flow value at `at`: {k: v, ...}, [a, b], or a scalar up to the next , } or ]. */
YamlNode flow(const std::string &s, size_t &at, int depth = 0) {
  skip_spaces(s, at);
  YamlNode n;
  if (depth > 32 || at >= s.size()) return n;
  const char open = s[at];
  if (open != '{' && open != '[') {
    const size_t start = at;
    char quote = 0;
    for (; at < s.size(); at++) {
      if (quote) quote = s[at] == quote ? 0 : quote;
      else if (s[at] == '\'' || s[at] == '"') quote = s[at];
      else if (s[at] == ',' || s[at] == '}' || s[at] == ']') break;
    }
    return scalar(s.substr(start, at - start));
  }
  at++;
  n.kind = open == '{' ? YamlNode::Map : YamlNode::Seq;
  const char close = open == '{' ? '}' : ']';
  while (at < s.size()) {
    skip_spaces(s, at);
    if (at < s.size() && s[at] == close) return at++, n;
    if (open == '[') n.seq.push_back(flow(s, at, depth + 1));
    if (open == '{') {
      const size_t colon = flow_colon(s, at);
      if (colon == std::string::npos) return n;
      const std::string key = unquote(s.substr(at, colon - at));
      at = colon + 1;
      n.map.emplace_back(key, flow(s, at, depth + 1));
    }
    skip_spaces(s, at);
    if (at < s.size() && s[at] == ',') at++;
    else if (at < s.size() && s[at] != close) at++;  // (a malformed entry: past it)
  }
  return n;
}

YamlNode inline_value(const std::string &rest) {
  size_t at = 0;
  return rest[0] == '{' || rest[0] == '[' ? flow(rest, at) : scalar(rest);
}

bool dash(const Line &l) { return l.text == "-" || l.text.rfind("- ", 0) == 0; }

YamlNode block(std::vector<Line> &lines, size_t &i, int depth);

/* A sequence of "- item" lines at `ind`. An item "key: value" is a mapping starting on its dash's line. */
YamlNode sequence(std::vector<Line> &lines, size_t &i, int ind, int depth) {
  YamlNode n;
  n.kind = YamlNode::Seq;
  while (i < lines.size() && lines[i].indent == ind && dash(lines[i])) {
    const std::string after = lines[i].text.substr(1);
    const size_t pad = after.find_first_not_of(' ');
    if (pad == std::string::npos) {  // ("-" alone: the item below it)
      i++;
      n.seq.push_back(i < lines.size() && lines[i].indent > ind ? block(lines, i, depth + 1) : YamlNode{});
      continue;
    }
    const std::string rest = after.substr(pad);
    if (rest[0] != '{' && rest[0] != '[' && rest[0] != '\'' && rest[0] != '"' && key_colon(rest) != std::string::npos) {
      lines[i] = Line{ind + 1 + (int)pad, rest};  // (the item's mapping, its first key where the dash's text began)
      n.seq.push_back(block(lines, i, depth + 1));
      continue;
    }
    n.seq.push_back(inline_value(rest));
    i++;
  }
  return n;
}

/* A plain or quoted scalar that carries on onto more-indented lines (Unity wraps long strings). */
void continue_scalar(std::vector<Line> &lines, size_t &i, int ind, YamlNode &v, std::string &raw) {
  const bool open_quote = (raw[0] == '"' || raw[0] == '\'') && (raw.size() < 2 || raw.back() != raw[0]);
  while (i < lines.size() && lines[i].indent > ind && (open_quote || (key_colon(lines[i].text) == std::string::npos && !dash(lines[i])))) {
    raw += " " + lines[i].text;
    i++;
    if (open_quote && raw.back() == raw[0]) break;
  }
  v = scalar(raw);
}

YamlNode mapping(std::vector<Line> &lines, size_t &i, int ind, int depth) {
  YamlNode n;
  n.kind = YamlNode::Map;
  while (i < lines.size() && lines[i].indent == ind && !dash(lines[i])) {
    const std::string t = lines[i].text;
    const size_t c = key_colon(t);
    i++;
    if (c == std::string::npos) continue;  // (a line that isn't a key: skipped)
    const std::string key = unquote(t.substr(0, c));
    std::string rest = trim(t.substr(c + 1));
    YamlNode v;
    if (!rest.empty() && rest[0] != '{' && rest[0] != '[') continue_scalar(lines, i, ind, v, rest);
    else if (!rest.empty()) v = inline_value(rest);
    if (!rest.empty())
      while (i < lines.size() && lines[i].indent > ind) i++;  // (review 370: a stray deeper line after a value - its siblings still read)
    else if (i < lines.size() && lines[i].indent > ind) v = block(lines, i, depth + 1);
    else if (i < lines.size() && lines[i].indent == ind && dash(lines[i])) v = sequence(lines, i, ind, depth + 1);  // (Unity's: the dashes at the key's indent)
    n.map.emplace_back(key, std::move(v));
  }
  return n;
}

YamlNode block(std::vector<Line> &lines, size_t &i, int depth) {
  if (i >= lines.size() || depth > 64) return YamlNode{};
  const int ind = lines[i].indent;
  return dash(lines[i]) ? sequence(lines, i, ind, depth) : mapping(lines, i, ind, depth);
}

/* "--- !u!1 &123 stripped": the class and the fileID. */
void read_header(const std::string &h, UnityDoc &d) {
  d.header = h;
  const size_t u = h.find("!u!"), amp = h.find('&');
  if (u != std::string::npos) d.class_id = std::atoi(h.c_str() + u + 3);
  if (amp != std::string::npos) d.file_id = std::strtoll(h.c_str() + amp + 1, nullptr, 10);
  d.stripped = h.find("stripped") != std::string::npos;
}

void finish_doc(UnityDoc &d) {
  YamlNode whole = parse_yaml(d.text);
  if (whole.kind != YamlNode::Map || whole.map.empty()) return;
  d.type = whole.map[0].first;
  d.body = std::move(whole.map[0].second);
}

}  // namespace

const YamlNode *YamlNode::get(const std::string &key) const {
  if (kind != Map) return nullptr;
  for (const auto &[k, v] : map)
    if (k == key) return &v;
  return nullptr;
}

std::string YamlNode::str(const std::string &key, const std::string &def) const {
  const YamlNode *n = get(key);
  return n && n->kind == Scalar ? n->value : def;
}

double YamlNode::num(const std::string &key, double def) const {
  const YamlNode *n = get(key);
  if (!n || n->kind != Scalar || n->value.empty()) return def;
  char *end = nullptr;
  const double v = std::strtod(n->value.c_str(), &end);
  return end && end != n->value.c_str() ? v : def;
}

int64_t YamlNode::file_id(const std::string &key) const {
  const YamlNode *n = get(key);
  return n ? std::strtoll(n->str("fileID", "0").c_str(), nullptr, 10) : 0;
}

std::string YamlNode::guid(const std::string &key) const {
  const YamlNode *n = get(key);
  return n ? n->str("guid") : std::string();
}

YamlNode parse_yaml(const std::string &text) {
  std::vector<Line> lines = lines_of(text);
  size_t i = 0;
  return block(lines, i, 0);
}

bool parse_unity_yaml(const std::string &text, std::vector<UnityDoc> &out, std::string &why) {
  if (text.rfind("%YAML", 0) != 0 && text.find("--- !u!") == std::string::npos) return why = "not Unity's YAML (a binary-serialized file?)", false;
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(at, end - at + (end < text.size() ? 1 : 0));
    at = end + 1;
    if (line.rfind("--- ", 0) == 0) {
      if (!out.empty()) finish_doc(out.back());
      out.emplace_back();
      read_header(line, out.back());
      continue;
    }
    if (!out.empty()) out.back().text += line;
  }
  if (!out.empty()) finish_doc(out.back());
  if (out.empty()) return why = "no documents in it", false;
  return true;
}

}  // namespace bl

// SPDX-License-Identifier: GPL-2.0-or-later
// A small XML reader (task 0141, for Collada): elements with their attributes, text and children, in document order.
// Enough for data files: comments, CDATA, processing instructions and the DOCTYPE are skipped, the five built-in
// entities and numeric references are decoded, namespaces are left in the names. No validation: a damaged file gives
// the elements read until the damage.
#pragma once

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace bl {

struct Xml {
  std::string name, text;  // the element's name; its own text (the pieces between its children, joined)
  std::vector<std::pair<std::string, std::string>> attrs;
  std::vector<Xml> children;
  const std::string &attr(const char *k) const {
    static const std::string none;
    for (const auto &kv : attrs)
      if (kv.first == k) return kv.second;
    return none;
  }
  /* The first child of that name (a null element when there is none: it can be queried further). */
  const Xml &child(const char *n) const {
    static const Xml none;
    for (const Xml &c : children)
      if (c.name == n) return c;
    return none;
  }
  std::vector<const Xml *> all(const char *n) const {
    std::vector<const Xml *> r;
    for (const Xml &c : children)
      if (c.name == n) r.push_back(&c);
    return r;
  }
  bool empty() const { return name.empty(); }
};

struct XmlParser {
  const char *p, *e;
  bool ok = true;
  int depth = 0;
  static constexpr int kMaxDepth = 256;
  XmlParser(const char *begin, const char *end) : p(begin), e(end) {}
  void ws() {
    while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
  }
  bool starts(const char *s) const {
    const size_t n = std::strlen(s);
    return (size_t)(e - p) >= n && std::memcmp(p, s, n) == 0;
  }
  void skip_past(const char *s) {
    const size_t n = std::strlen(s);
    while (p < e && !starts(s)) p++;
    if (p < e) p += n;
  }
  static void decode(const std::string &in, std::string &out) {
    for (size_t i = 0; i < in.size(); i++) {
      if (in[i] != '&') {
        out += in[i];
        continue;
      }
      const size_t semi = in.find(';', i);
      if (semi == std::string::npos || semi - i > 10) {
        out += in[i];
        continue;
      }
      const std::string ent = in.substr(i + 1, semi - i - 1);
      if (ent == "lt") out += '<';
      else if (ent == "gt") out += '>';
      else if (ent == "amp") out += '&';
      else if (ent == "quot") out += '"';
      else if (ent == "apos") out += '\'';
      else if (ent.size() > 1 && ent[0] == '#') {
        const unsigned long c = std::strtoul(ent.c_str() + (ent[1] == 'x' ? 2 : 1), nullptr, ent[1] == 'x' ? 16 : 10);
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) out += (char)(0xC0 | c >> 6), out += (char)(0x80 | (c & 0x3F));
        else if (c < 0x10000) out += (char)(0xE0 | c >> 12), out += (char)(0x80 | (c >> 6 & 0x3F)), out += (char)(0x80 | (c & 0x3F));
        else out += (char)(0xF0 | c >> 18), out += (char)(0x80 | (c >> 12 & 0x3F)), out += (char)(0x80 | (c >> 6 & 0x3F)), out += (char)(0x80 | (c & 0x3F));
      }
      else out += '&' + ent + ';';
      i = semi;
    }
  }
  std::string name() {
    const char *s = p;
    while (p < e && !(*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == '>' || *p == '/' || *p == '=')) p++;
    return std::string(s, (size_t)(p - s));
  }
  /* The next element at this point (after a '<'), its children with it. */
  bool element(Xml &out) {
    if (++depth > kMaxDepth) return ok = false;
    out.name = name();
    if (out.name.empty()) return ok = false;
    for (;;) {
      ws();
      if (p >= e) return ok = false;
      if (*p == '/') {  // <a .../>
        p++;
        skip_past(">");
        depth--;
        return true;
      }
      if (*p == '>') {
        p++;
        break;
      }
      const std::string k = name();
      ws();
      if (p >= e || *p != '=' || k.empty()) return ok = false;
      p++;
      ws();
      if (p >= e || (*p != '"' && *p != '\'')) return ok = false;
      const char q = *p++;
      const char *s = p;
      while (p < e && *p != q) p++;
      std::string v;
      decode(std::string(s, (size_t)(p - s)), v);
      out.attrs.push_back({k, v});
      if (p < e) p++;
    }
    /* The content: text and children until </name>. */
    for (;;) {
      const char *s = p;
      while (p < e && *p != '<') p++;
      if (p > s) decode(std::string(s, (size_t)(p - s)), out.text);
      if (p >= e) return ok = false;
      p++;  // '<'
      if (starts("/")) {  // (any closing tag closes this element: enough for data files, no validation)
        skip_past(">");
        depth--;
        return true;
      }
      if (starts("!--")) {
        skip_past("-->");
        continue;
      }
      if (starts("![CDATA[")) {
        p += 8;
        const char *c = p;
        while (p < e && !starts("]]>")) p++;
        out.text.append(c, (size_t)(p - c));
        if (p >= e) return ok = false;  // (cut short inside the CDATA)
        p += 3;
        continue;
      }
      if (starts("?") || starts("!")) {
        skip_past(">");
        continue;
      }
      Xml child;
      if (!element(child)) return false;
      out.children.push_back(std::move(child));
    }
  }
  /* The document's root element (after any prolog). */
  bool document(Xml &root) {
    for (;;) {
      ws();
      if (p >= e || *p != '<') return ok = false;
      p++;
      if (starts("?") || starts("!")) {
        if (starts("!--")) skip_past("-->");
        else skip_past(">");
        continue;
      }
      return element(root);
    }
  }
};

}  // namespace bl

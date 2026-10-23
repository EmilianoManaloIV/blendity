// SPDX-License-Identifier: GPL-2.0-or-later
// A small JSON reader (glTF's, and GitHub's releases list for the updater, task 0079): Json is the parsed value,
// JsonParser{begin, end}.value() parses one; `ok` false on malformed input (the value is then partial).
#pragma once

#include "../../extern/fast_float/fast_float.h"

#include <cstdint>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace bl {

struct Json {
  enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
  double num = 0;
  bool b = false;
  std::string str;
  std::vector<Json> arr;
  std::vector<std::pair<std::string, Json>> obj;
  const Json &operator[](const char *k) const {
    static const Json null;
    for (auto &kv : obj)
      if (kv.first == k) return kv.second;
    return null;
  }
  const Json &operator[](size_t i) const {
    static const Json null;
    return i < arr.size() ? arr[i] : null;
  }
  const Json &operator[](int i) const { return (*this)[(size_t)i]; }
  double number(double def = 0) const { return type == Num ? num : def; }
  int integer(int def = -1) const { return type == Num ? (int)num : def; }
  size_t size() const { return type == Arr ? arr.size() : 0; }
};

struct JsonParser {
  const char *p, *e;
  bool ok = true;
  void ws() {
    while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
  }
  bool lit(const char *s) {
    size_t n = std::strlen(s);
    if ((size_t)(e - p) >= n && std::memcmp(p, s, n) == 0) {
      p += n;
      return true;
    }
    return false;
  }
  std::string string() {
    std::string s;
    p++;  // opening quote
    while (p < e && *p != '"') {
      if (*p == '\\' && p + 1 < e) {
        p++;
        char c = *p++;
        if (c == 'n') s += '\n';
        else if (c == 't') s += '\t';
        else if (c == 'r') s += '\r';
        else if (c == 'b') s += '\b';
        else if (c == 'f') s += '\f';
        else if (c == 'u' && e - p >= 4) {
          uint32_t cp = (uint32_t)std::strtoul(std::string(p, p + 4).c_str(), nullptr, 16);
          p += 4;
          if (cp >= 0xD800 && cp < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
            uint32_t lo = (uint32_t)std::strtoul(std::string(p + 2, p + 6).c_str(), nullptr, 16);
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            p += 6;
          }
          if (cp < 0x80) s += (char)cp;
          else if (cp < 0x800) s += (char)(0xC0 | (cp >> 6)), s += (char)(0x80 | (cp & 63));
          else if (cp < 0x10000) s += (char)(0xE0 | (cp >> 12)), s += (char)(0x80 | ((cp >> 6) & 63)), s += (char)(0x80 | (cp & 63));
          else
            s += (char)(0xF0 | (cp >> 18)), s += (char)(0x80 | ((cp >> 12) & 63)), s += (char)(0x80 | ((cp >> 6) & 63)),
                s += (char)(0x80 | (cp & 63));
        }
        else s += c;
      }
      else s += *p++;
    }
    if (p < e) p++;
    else ok = false;
    return s;
  }
  Json value(int depth = 0) {
    Json j;
    ws();
    if (p >= e || depth > 256) {
      ok = false;
      return j;
    }
    if (*p == '{') {
      j.type = Json::Obj;
      p++;
      ws();
      if (p < e && *p == '}') {
        p++;
        return j;
      }
      while (ok && p < e) {
        ws();
        if (p >= e || *p != '"') {
          ok = false;
          break;
        }
        std::string k = string();
        ws();
        if (p >= e || *p != ':') {
          ok = false;
          break;
        }
        p++;
        j.obj.emplace_back(std::move(k), value(depth + 1));
        ws();
        if (p < e && *p == ',') p++;
        else if (p < e && *p == '}') {
          p++;
          break;
        }
        else ok = false;
      }
    }
    else if (*p == '[') {
      j.type = Json::Arr;
      p++;
      ws();
      if (p < e && *p == ']') {
        p++;
        return j;
      }
      while (ok && p < e) {
        j.arr.push_back(value(depth + 1));
        ws();
        if (p < e && *p == ',') p++;
        else if (p < e && *p == ']') {
          p++;
          break;
        }
        else ok = false;
      }
    }
    else if (*p == '"') {
      j.type = Json::Str;
      j.str = string();
    }
    else if (lit("true")) j.type = Json::Bool, j.b = true;
    else if (lit("false")) j.type = Json::Bool;
    else if (lit("null")) {}
    else {
      j.type = Json::Num;
      auto r = fast_float::from_chars(p, e, j.num, fast_float::chars_format::general);
      if (r.ec != std::errc()) ok = false;
      else p = r.ptr;
    }
    return j;
  }
};


/* JSON on one line (task 0172: Live Link's messages): w.obj().key("t").str("hello").key("n").num(3).end(); w.out.
 * Numbers that aren't finite are written as 0 (JSON has no NaN). */
struct JsonWriter {
  std::string out;
  JsonWriter() { out.reserve(192); }
  JsonWriter &obj() { return open('{'); }
  JsonWriter &arr() { return open('['); }
  JsonWriter &end() {
    out += stack_.empty() ? '}' : stack_.back();
    if (!stack_.empty()) stack_.pop_back();
    first_ = false;
    return *this;
  }
  JsonWriter &key(const char *k) {
    sep();
    quoted(k, std::strlen(k));
    out += ':';
    after_key_ = true;
    return *this;
  }
  JsonWriter &str(const std::string &s) {
    sep();
    quoted(s.data(), s.size());
    return *this;
  }
  JsonWriter &num(double v) {
    sep();
    char b[32];
    std::snprintf(b, sizeof(b), "%.9g", v == v && v - v == 0 ? v : 0.0);
    out += b;
    return *this;
  }
  JsonWriter &integer(int64_t v) {
    sep();
    out += std::to_string(v);
    return *this;
  }
  JsonWriter &boolean(bool v) {
    sep();
    out += v ? "true" : "false";
    return *this;
  }
  JsonWriter &raw(const std::string &json) {  // an already-written value
    sep();
    out += json;
    return *this;
  }
  JsonWriter &num(float v) {  // the shortest text that reads back as the same float
    sep();
    char b[32];
    const float f = v == v && v - v == 0 ? v : 0.0f;
#if defined(__cpp_lib_to_chars) || (defined(_MSC_VER) && _MSC_VER >= 1924)
    const auto r = std::to_chars(b, b + sizeof(b), f);
    out.append(b, r.ptr);
#else
    std::snprintf(b, sizeof(b), "%.9g", (double)f);  // (Apple's libc++ before macOS 13.3: no float to_chars)
    out += b;
#endif
    return *this;
  }
  JsonWriter &nums(const float *v, int n) {
    arr();
    for (int i = 0; i < n; i++) num(v[i]);
    return end();
  }

 private:
  std::string stack_;  // the closing brackets owed
  bool first_ = true, after_key_ = false;
  JsonWriter &open(char c) {
    sep();
    out += c;
    stack_ += c == '{' ? '}' : ']';
    first_ = true;
    return *this;
  }
  void sep() {
    if (after_key_) {
      after_key_ = false;
      return;
    }
    if (!first_) out += ',';
    first_ = false;
  }
  void quoted(const char *s, size_t n) {
    out += '"';
    size_t plain = 0;  // (most text needs no escape: appended whole)
    while (plain < n && (unsigned char)s[plain] >= 0x20 && s[plain] != '"' && s[plain] != '\\') plain++;
    out.append(s, plain);
    for (size_t i = plain; i < n; i++) {
      const unsigned char c = (unsigned char)s[i];
      if (c == '"' || c == '\\') out += '\\', out += (char)c;
      else if (c == '\n') out += "\\n";
      else if (c == '\r') out += "\\r";
      else if (c == '\t') out += "\\t";
      else if (c < 0x20) {
        char b[8];
        std::snprintf(b, sizeof(b), "\\u%04x", c);
        out += b;
      }
      else out += (char)c;
    }
    out += '"';
  }
};

}  // namespace bl

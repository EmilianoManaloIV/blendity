// SPDX-License-Identifier: GPL-2.0-or-later
#include "source2_resource.h"

#include <stdexcept>

#ifdef BL_WITH_ZSTD
#  include <zstd.h>
#endif

namespace bl::source2 {
namespace {

constexpr size_t kMaxDecodedBytes = 256 * 1024 * 1024;
constexpr uint32_t kTrailer = 0xFFEEDD00;

void require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}

Bytes slice(Bytes bytes, size_t start, size_t count) {
  require(start <= bytes.size() && count <= bytes.size() - start, "truncated Source 2 block");
  return bytes.substr(start, count);
}

struct Reader {
  Bytes bytes;

  Bytes take(size_t count) {
    Bytes result = slice(bytes, 0, count);
    bytes.remove_prefix(count);
    return result;
  }

  template<class T> T read() {
    T value;
    std::memcpy(&value, take(sizeof(T)).data(), sizeof(T));
    return value;
  }

  std::string_view string() {
    require(!bytes.empty(), "unterminated Source 2 string");
    size_t length = bytes.find(uint8_t(0));
    require(length != Bytes::npos, "unterminated Source 2 string");
    return {reinterpret_cast<const char *>(take(length + 1).data()), length};
  }
};

std::vector<uint8_t> unpack(Reader &reader, uint32_t method, size_t size, size_t stored) {
  if (method > 2) throw std::runtime_error("unsupported Source 2 compression method " + std::to_string(method));
  require(size <= kMaxDecodedBytes, "Source 2 packed buffer is too large");
  Bytes input = reader.take(method == 0 ? size : stored);
  if (method == 0) return input.empty() ? std::vector<uint8_t>() : std::vector<uint8_t>(input.begin(), input.end());
  std::vector<uint8_t> output(size);
  if (method == 1) {
    size_t written = 0;
    require(decode_lz4(input, output, written) && written == size, "invalid Source 2 LZ4 data");
  }
  else {
#ifdef BL_WITH_ZSTD
    size_t written = ZSTD_decompress(output.data(), output.size(), input.data(), input.size());
    require(!ZSTD_isError(written) && written == size, "invalid Source 2 Zstandard data");
#else
    throw std::runtime_error("Source 2 Zstandard data requires Blender's libraries");
#endif
  }
  return output;
}

Bytes view(const std::vector<uint8_t> &bytes) {
  return bytes.empty() ? Bytes{} : Bytes(bytes.data(), bytes.size());
}

struct LaneSizes {
  size_t one = 0, two = 0, four = 0, eight = 0;
};

struct Lanes {
  Reader one, two, four, eight;
};

Lanes take_lanes(Bytes bytes, size_t &offset, LaneSizes counts, bool align_empty_tail) {
  auto lane = [&](size_t count, size_t width) -> Reader {
    if (!count) return {};
    require(count <= bytes.size() / width, "invalid KV3 lane size");
    offset = (offset + width - 1) & ~(width - 1);
    auto result = slice(bytes, offset, count * width);
    offset += result.size();
    return {result};
  };
  Lanes result{lane(counts.one, 1), lane(counts.two, 2), lane(counts.four, 4), lane(counts.eight, 8)};
  if (align_empty_tail) offset = (offset + 7) & ~size_t(7);
  require(offset <= bytes.size(), "invalid KV3 padding");
  return result;
}

struct KvHeader {
  int version = 0;
  uint32_t method = 0;
  uint16_t frame = 0;
  LaneSizes lanes, value_lanes;
  size_t types = 0, blob_count = 0, blob_bytes = 0, objects = 0;
  size_t size1 = 0, size2 = 0, stored1 = 0, stored2 = 0, stored_total = 0;
};

KvHeader read_header(Reader &input) {
  KvHeader h;
  const uint32_t magic = input.read<uint32_t>();
  h.version = int(magic & 255);
  require((magic & 0xFFFFFF00) == 0x4B563300, "unsupported Source 2 DATA encoding (expected binary KV3)");
  if (h.version < 1 || h.version > 5)
    throw std::runtime_error("unsupported binary KV3 version " + std::to_string(h.version));
  input.take(16);  // format GUID
  h.method = input.read<uint32_t>();
  if (h.version >= 2) {
    require(input.read<uint16_t>() == 0, "unsupported KV3 compression dictionary");
    h.frame = input.read<uint16_t>();
  }
  h.lanes.one = input.read<uint32_t>();
  h.lanes.four = input.read<uint32_t>();
  h.lanes.eight = input.read<uint32_t>();
  if (h.version >= 2) {
    h.types = input.read<uint32_t>();
    input.take(4);  // object and array allocation hints
  }
  h.size1 = input.read<uint32_t>();
  h.stored1 = h.stored_total = h.version == 1 ? input.bytes.size() : input.read<uint32_t>();
  if (h.version >= 2) {
    h.blob_count = input.read<uint32_t>();
    h.blob_bytes = input.read<uint32_t>();
  }
  if (h.version >= 4) {
    h.lanes.two = input.read<uint32_t>();
    input.take(4);  // total byte count of compressed blob frame lengths
  }
  if (h.version >= 5) {
    h.size1 = input.read<uint32_t>();
    h.stored1 = input.read<uint32_t>();
    h.size2 = input.read<uint32_t>();
    h.stored2 = input.read<uint32_t>();
    h.value_lanes.one = input.read<uint32_t>();
    h.value_lanes.two = input.read<uint32_t>();
    h.value_lanes.four = input.read<uint32_t>();
    h.value_lanes.eight = input.read<uint32_t>();
    input.take(4);  // node count
    h.objects = input.read<uint32_t>();
    input.take(8);  // array allocation hints
  }
  require(h.size1 <= kMaxDecodedBytes && h.size2 <= kMaxDecodedBytes - h.size1 &&
              h.blob_bytes <= kMaxDecodedBytes - h.size1 - h.size2,
          "KV3 decoded data is too large");
  return h;
}

struct KvValues {
  int version = 0;
  Reader types, objects, blob_lengths, blobs;
  Lanes main, aux;
  std::vector<std::string_view> strings;
  size_t remaining = 1000000;  // also bounds typed arrays of constants, which have no encoded payload per element
  size_t string_bytes_left = 0;

  void charge_string(size_t size) {
    require(size <= string_bytes_left, "KV3 string and blob expansion exceeds its budget");
    string_bytes_left -= size;
  }

  uint8_t type() {
    uint8_t value = types.read<uint8_t>();
    if (value & 128) types.take(1);
    if (version >= 3 && (value & 64)) types.take(1);
    return value & (version >= 3 ? 63 : 127);
  }

  std::string_view string() {
    uint32_t index = main.four.read<uint32_t>();
    if (index == UINT32_MAX) return {};
    require(index < strings.size(), "invalid KV3 string index");
    charge_string(strings[index].size());
    return strings[index];
  }

  Json value(uint8_t code, Lanes &lane, int depth);
};

template<class T> void read_wide_integer(Json &value, Reader &lane) {
  const T number = lane.read<T>();
  const uint64_t bits = static_cast<uint64_t>(number);
  value.num = static_cast<double>(number);
  // Json's numeric view is a double. Retain the wire bits in its otherwise unused string field.
  value.str.assign(reinterpret_cast<const char *>(&bits), sizeof(bits));
}

Json KvValues::value(uint8_t code, Lanes &lane, int depth) {
  require(depth < 128 && remaining > 0, "KV3 document is too complex");
  --remaining;
  Json result;
  result.type = Json::Num;
  switch (code) {
    case 1: return {};
    case 2: case 13: case 14:
      result.type = Json::Bool;
      result.b = code == 2 ? lane.one.read<uint8_t>() != 0 : code == 13;
      break;
    case 3: charge_string(8); read_wide_integer<int64_t>(result, lane.eight); break;
    case 4: charge_string(8); read_wide_integer<uint64_t>(result, lane.eight); break;
    case 5: result.num = lane.eight.read<double>(); break;
    case 6: result.type = Json::Str; result.str = string(); break;
    case 7: {
      // Keep the raw bytes in the string storage. Mesh descriptors use blobs for buffer data and semantic names.
      Reader &data = version == 1 ? main.one : blobs;
      size_t length = (version == 1 ? main.four : blob_lengths).read<uint32_t>();
      charge_string(length);
      Bytes raw = data.take(length);
      result.type = Json::Str;
      if (!raw.empty()) result.str.assign(reinterpret_cast<const char *>(raw.data()), raw.size());
      break;
    }
    case 8: case 10: case 24: case 25: {
      result.type = Json::Arr;
      size_t count = code < 24 ? main.four.read<uint32_t>() : main.one.read<uint8_t>();
      require(count <= remaining, "invalid KV3 array length");
      uint8_t child = code == 8 ? 0 : type();
      Lanes &elements = code == 25 ? aux : main;
      for (size_t i = 0; i < count; ++i)
        result.arr.push_back(value(code == 8 ? type() : child, elements, depth + 1));
      break;
    }
    case 9: {
      result.type = Json::Obj;
      size_t count = (version >= 5 ? objects : main.four).read<uint32_t>();
      require(count <= remaining && count <= types.bytes.size(), "invalid KV3 object length");
      for (size_t i = 0; i < count; ++i) {
        uint8_t child = type();
        auto key = string();
        result.obj.emplace_back(key, value(child, main, depth + 1));
      }
      break;
    }
    case 11: result.num = lane.four.read<int32_t>(); break;
    case 12: result.num = lane.four.read<uint32_t>(); break;
    case 15: case 17: case 26: result.num = 0; break;
    case 16: case 18: case 27: result.num = 1; break;
    case 19: result.num = lane.four.read<float>(); break;
    case 20: result.num = lane.two.read<int16_t>(); break;
    case 21: result.num = lane.two.read<uint16_t>(); break;
    case 22: result.num = lane.one.read<int8_t>(); break;
    case 23: result.num = lane.one.read<uint8_t>(); break;
    default: throw std::runtime_error("unsupported KV3 value type " + std::to_string(code));
  }
  return result;
}

void read_strings(Reader &reader, size_t count, std::vector<std::string_view> &out) {
  require(count <= reader.bytes.size() && count <= 1000000, "invalid KV3 string count");
  out.reserve(count);
  for (size_t i = 0; i < count; ++i) out.push_back(reader.string());
}

std::vector<uint8_t> read_blobs(Reader &input, Reader &tail, const KvHeader &h, KvValues &values, Bytes old_zstd_blobs) {
  require(h.blob_count <= tail.bytes.size() / 4, "invalid KV3 blob count");
  values.blob_lengths = {tail.take(h.blob_count * 4)};
  require(tail.read<uint32_t>() == kTrailer, "invalid KV3 buffer trailer");
  if (!h.blob_count) return {};

  std::vector<uint8_t> output;
  if (h.method == 0) values.blobs = {input.take(h.blob_bytes)};
  else if (h.method == 2 && h.version < 5) values.blobs = {old_zstd_blobs};
  else if (h.method == 2) {
    require(h.stored1 + h.stored2 <= h.stored_total, "invalid KV3 compressed blob size");
    output = unpack(input, h.method, h.blob_bytes, h.stored_total - h.stored1 - h.stored2);
  }
  else {
    require(h.method == 1 && h.frame > 0, "unsupported KV3 blob compression");
    output.resize(h.blob_bytes);
    Reader lengths = values.blob_lengths;
    size_t written = 0;
    for (size_t i = 0; i < h.blob_count; ++i) {
      size_t length = lengths.read<uint32_t>();
      require(length <= output.size() - written, "invalid KV3 blob length");
      size_t end = written + length;
      while (written < end) {
        size_t next = std::min(end, written + h.frame);
        Bytes packed = input.take(tail.read<uint16_t>());
        require(decode_lz4(packed, output, written, next) && written == next, "invalid KV3 LZ4 blob frame");
      }
    }
    require(written == output.size(), "invalid KV3 blob totals");
  }
  require(input.read<uint32_t>() == kTrailer, "invalid KV3 blob trailer");
  return output;
}

}  // namespace

bool kv3_integer_bits(const Json &value, uint64_t &bits) {
  if (value.type != Json::Num) return false;
  if (value.str.size() == sizeof(bits)) {
    std::memcpy(&bits, value.str.data(), sizeof(bits));
    return true;
  }
  const double number = value.num;
  if (!finite_bits(number) || number < -0x1p63 || number >= 0x1p64 || std::floor(number) != number) return false;
  bits = number < 0 ? static_cast<uint64_t>(static_cast<int64_t>(number)) : static_cast<uint64_t>(number);
  return true;
}

bool decode_lz4(Bytes input, std::vector<uint8_t> &output, size_t &written, size_t limit) {
  if (written > output.size()) return false;
  limit = std::min(limit, output.size());
  size_t cursor = 0;
  auto length = [&](size_t base) {
    if (base != 15) return base;
    while (cursor < input.size()) {
      uint8_t more = input[cursor++];
      base += more;
      if (more != 255) return base;
    }
    return size_t(-1);
  };
  while (cursor < input.size()) {
    uint8_t token = input[cursor++];
    size_t literals = length(token >> 4);
    if (written > limit || literals > input.size() - cursor || literals > limit - written) return false;
    if (literals) std::memcpy(output.data() + written, input.data() + cursor, literals);
    cursor += literals;
    written += literals;
    if (cursor == input.size()) return true;
    if (input.size() - cursor < 2) return false;
    size_t distance = input[cursor] | size_t(input[cursor + 1]) << 8;
    cursor += 2;
    size_t match = length(token & 15);
    if (!distance || distance > written || limit - written < 4 || match > limit - written - 4) return false;
    for (size_t i = 0; i < match + 4; ++i, ++written) output[written] = output[written - distance];
  }
  return written <= limit;
}

Bytes Resource::block(std::string_view name) const {
  for (const auto &entry : blocks)
    if (entry.first == name) return entry.second;
  return {};
}

bool read_resource(const std::vector<uint8_t> &file, Resource &out, std::string &error) {
  try {
    Reader header{view(file)};
    Resource result;
    result.file_size = header.read<uint32_t>();
    uint16_t version = header.read<uint16_t>();
    if (version != 12) throw std::runtime_error("unsupported Source 2 resource header version " + std::to_string(version));
    result.version = header.read<uint16_t>();
    size_t offset = size_t(header.read<uint32_t>()) + 8;
    size_t count = header.read<uint32_t>();
    require(result.file_size >= 16 && result.file_size <= file.size(), "invalid Source 2 resource file size");
    require(offset >= 16 && offset <= result.file_size && count <= (result.file_size - offset) / 12,
            "invalid Source 2 resource directory");
    Bytes bytes = view(file).substr(0, result.file_size);
    Reader table{slice(bytes, offset, count * 12)};
    result.blocks.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      Bytes tag = table.take(4);
      size_t start = offset + i * 12 + 4 + table.read<uint32_t>();
      size_t length = table.read<uint32_t>();
      result.blocks.emplace_back(std::string(reinterpret_cast<const char *>(tag.data()), tag.size()), slice(bytes, start, length));
    }
    out = std::move(result);
    return true;
  }
  catch (const std::runtime_error &e) {
    error = e.what();
    return false;
  }
}

bool read_references(const Resource &resource, std::vector<std::pair<uint64_t, std::string>> &out, std::string &error) {
  try {
    Bytes bytes = resource.block("RERL");
    std::vector<std::pair<uint64_t, std::string>> result;
    if (!bytes.empty()) {
      Reader header{bytes};
      size_t start = header.read<uint32_t>();
      size_t count = header.read<uint32_t>();
      require(start <= bytes.size() && count <= (bytes.size() - start) / 16, "invalid Source 2 external references");
      Reader table{slice(bytes, start, count * 16)};
      for (size_t i = 0; i < count; ++i) {
        uint64_t id = table.read<uint64_t>();
        int64_t field = int64_t(start + i * 16 + 8) + table.read<int32_t>();
        table.take(4);
        require(field >= 0 && uint64_t(field) < bytes.size(), "invalid Source 2 reference string offset");
        Reader name{bytes.substr(size_t(field))};
        result.emplace_back(id, name.string());
      }
    }
    out = std::move(result);
    return true;
  }
  catch (const std::runtime_error &e) {
    error = e.what();
    return false;
  }
}

bool read_kv3(Bytes bytes, Json &out, std::string &error) {
  try {
    Reader input{bytes};
    KvHeader h = read_header(input);
    size_t old_zstd_size = h.method == 2 && h.version < 5 ? h.blob_bytes : 0;
    auto first = unpack(input, h.method, h.size1 + old_zstd_size, h.stored1);
    Bytes first_data = view(first).substr(0, h.size1);
    size_t offset = 0;
    Lanes first_lanes = take_lanes(first_data, offset, h.lanes, h.version < 5);
    size_t count = first_lanes.four.read<uint32_t>();
    KvValues values;
    values.version = h.version;
    // Repeated string IDs otherwise let a small document duplicate large keys or values arbitrarily.
    constexpr size_t max_strings = 128 * 1024 * 1024;
    const size_t decoded = h.size1 + h.size2 + h.blob_bytes;
    values.string_bytes_left = std::min(max_strings, std::min(decoded, max_strings / 32) * 32 + 64 * 1024);
    Reader tail{first_data.substr(offset)};
    std::vector<uint8_t> second;
    if (h.version < 5) {
      values.main = first_lanes;
      size_t before = tail.bytes.size();
      read_strings(tail, count, values.strings);
      size_t strings_size = before - tail.bytes.size();
      require(h.version == 1 ? tail.bytes.size() >= 4 : h.types >= strings_size, "invalid KV3 types size");
      values.types = {tail.take(h.version == 1 ? tail.bytes.size() - 4 : h.types - strings_size)};
    }
    else {
      values.aux = first_lanes;
      read_strings(values.aux.one, count, values.strings);
      second = unpack(input, h.method, h.size2, h.stored2);
      require(h.objects <= h.size2 / 4, "invalid KV3 object table");
      offset = h.objects * 4;
      values.objects = {view(second).substr(0, offset)};
      values.main = take_lanes(view(second), offset, h.value_lanes, false);
      tail = {view(second).substr(offset)};
      values.types = {tail.take(h.types)};
    }
    auto blobs = read_blobs(input, tail, h, values, view(first).substr(h.size1));
    if (!blobs.empty()) values.blobs = {view(blobs)};
    Json result = values.value(values.type(), values.main, 0);
    require(values.types.bytes.empty() && values.blob_lengths.bytes.empty() && values.blobs.bytes.empty(),
            "unconsumed KV3 types or blobs");
    out = std::move(result);
    return true;
  }
  catch (const std::runtime_error &e) {
    error = e.what();
    return false;
  }
}

}  // namespace bl::source2

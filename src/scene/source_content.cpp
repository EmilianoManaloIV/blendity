// SPDX-License-Identifier: GPL-2.0-or-later
#include "source_content.h"

#include "../core/core.h"
#include "../image/image.h"
#include "import.h"
#include "unity_package.h"  // crc32

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace bl {
namespace {
namespace disk = std::filesystem;

constexpr size_t kMaxResource = 512u << 20;
constexpr size_t kMaxIndex = 64u << 20;
constexpr size_t kMaxFiles = 250000;
constexpr size_t kMaxPath = 1024;
constexpr size_t kMaxArchiveBytes = 4 * kMaxIndex;
constexpr size_t kMaxArchiveEntries = 4 * kMaxFiles;
constexpr size_t kMaxNestedPackages = 128;
constexpr size_t kMaxDiscoveredPackages = 256;
constexpr int kMaxNestedDepth = 4;
constexpr size_t kMaxMountWarnings = 32;
constexpr uint16_t kVpkInline = 0x7fff;

bool fail(std::string &error, const std::string &message) {
  error = message;
  return false;
}

std::string virtual_key(std::string path) {
  if (path.empty() || path.size() > kMaxPath || path.front() == '/' || path.front() == '\\') return {};
  for (char &ch : path) {
    if ((unsigned char)ch < 32 || std::strchr(":*?\"<>|", ch)) return {};
    if (ch == '\\') ch = '/';
    if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
  }
  size_t begin = 0;
  for (;;) {
    const size_t end = path.find('/', begin);
    const size_t size = (end == std::string::npos ? path.size() : end) - begin;
    if (!size || path.compare(begin, size, ".") == 0 || path.compare(begin, size, "..") == 0) return {};
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return path;
}

std::string extension(const std::string &key) {
  const size_t dot = key.rfind('.'), slash = key.rfind('/');
  return dot != std::string::npos && (slash == std::string::npos || dot > slash) ? key.substr(dot) : std::string{};
}

uint16_t le16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t le32(const uint8_t *p) { return uint32_t(le16(p)) | uint32_t(le16(p + 2)) << 16; }

bool fits(uint64_t offset, uint64_t size, uint64_t end) { return offset <= end && size <= end - offset; }

uint64_t disk_size(const disk::path &path) {
  std::error_code ec;
  const auto size = disk::file_size(path, ec);
  return ec ? 0 : size;
}

bool read_range(const disk::path &path, uint64_t offset, size_t size, std::vector<uint8_t> &bytes) {
  std::error_code ec;
  if (!disk::is_regular_file(path, ec) || !fits(offset, size, disk_size(path))) return false;
  std::ifstream in(path, std::ios::binary);
  if (!in.seekg((std::streamoff)offset)) return false;
  const size_t first = bytes.size();
  bytes.resize(first + size);
  return !size || bool(in.read(reinterpret_cast<char *>(bytes.data() + first), (std::streamsize)size));
}

bool beneath(const disk::path &parent, const disk::path &file) {
  const disk::path rel = file.lexically_relative(parent);
  return !rel.empty() && !rel.is_absolute() && *rel.begin() != "..";
}

uint32_t checksum(const std::vector<uint8_t> &bytes) {
  uint32_t crc = 0;
  for (size_t i = 0; i < bytes.size(); i += 65536)
    crc = crc32(std::string(reinterpret_cast<const char *>(bytes.data() + i), std::min<size_t>(65536, bytes.size() - i)), crc);
  return crc;
}

bool numbered_vpk_stem(const std::string &stem) {
  const size_t underscore = stem.rfind('_');
  return underscore != std::string::npos && stem.size() - underscore >= 4 &&
         std::all_of(stem.begin() + underscore + 1, stem.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::string numbered_vpk_index(const std::string &stem, const std::string &extension) {
  return numbered_vpk_stem(stem) ? stem.substr(0, stem.rfind('_')) + "_dir" + extension : std::string{};
}

disk::path numbered_vpk_index(const disk::path &file) {
  const std::string name = numbered_vpk_index(file.stem().u8string(), file.extension().u8string());
  return name.empty() ? disk::path{} : file.parent_path() / disk::u8path(name);
}

bool vpk_directory_exists(const disk::path &path) {
  std::error_code ec;
  return !path.empty() && disk::is_regular_file(path, ec);
}

bool vpk_signature(const disk::path &file) {
  std::vector<uint8_t> header;
  return read_range(file, 0, 4, header) && le32(header.data()) == 0x55aa1234;
}

struct MountedFile {
  disk::path local;
  uint64_t offset = 0;
  uint32_t size = 0, unpacked_size = 0, crc = 0;
  uint32_t preload_offset = 0;
  uint16_t preload_size = 0;
  uint16_t chunk = kVpkInline;
  uint16_t method = 0;

  uint64_t vpk_length() const { return uint64_t(preload_size) + size; }
};

struct ArchiveBudget {
  size_t bytes = 0, entries = 0, nested = 0;
};

std::string chunk_name(const std::string &index_name, uint16_t chunk) {
  const size_t suffix = index_name.size();
  if (suffix < 8 || to_lower(index_name.substr(suffix - 8)) != "_dir.vpk") return {};
  return index_name.substr(0, suffix - 8) + strprintf("_%03u", unsigned(chunk)) + index_name.substr(suffix - 4);
}

bool tree_string(const std::vector<uint8_t> &tree, size_t &cursor, std::string &word) {
  const size_t first = cursor;
  while (cursor < tree.size() && cursor - first <= kMaxPath && tree[cursor] != 0) ++cursor;
  if (cursor == tree.size() || cursor - first > kMaxPath) return false;
  word.assign(reinterpret_cast<const char *>(tree.data() + first), cursor - first);
  ++cursor;
  return true;
}
}  // namespace

struct SourceContent::Mount {
  enum Kind { Loose, Vpk, Zip } kind = Loose;
  disk::path local;
  std::string label;
  std::map<std::string, MountedFile> files;
  std::vector<uint8_t> index;  // VPK directory tree (contains preloads), or the map's embedded ZIP
  std::shared_ptr<const Mount> parent;
  size_t lookup_bytes = 0;
  std::string package_path;  // nested VPK's path in the parent
  const MountedFile *container = nullptr;  // stable in the parent's immutable index

  static void discover_children(const std::shared_ptr<const Mount> &outer, int depth, ArchiveBudget &budget,
                                std::vector<std::shared_ptr<const Mount>> &children, std::vector<std::string> &warnings);
  bool load_vpk(ArchiveBudget &budget, std::string &error);
  bool read_archive_range(uint16_t chunk, uint64_t offset, size_t size, std::vector<uint8_t> &bytes) const;
  bool read_vpk_range(const MountedFile &f, uint64_t offset, size_t size, std::vector<uint8_t> &bytes) const;
  bool load_zip(const std::vector<uint8_t> &zip, std::string &error);
  bool read(const MountedFile &f, std::vector<uint8_t> &bytes) const;
};

std::string source_loose_fingerprint(const std::string &path) {
  const auto file = disk::u8path(path);
  std::error_code ec;
  const uint64_t length = disk::file_size(file, ec);
  if (ec || !disk::is_regular_file(file, ec) || length > kMaxResource) return {};
  std::ifstream input(file, std::ios::binary);
  if (!input) return {};
  uint8_t buffer[65536];
  uint64_t state = kFnvBasis;
  for (uint64_t i = 0; i < length;) {
    const auto count = (size_t)std::min<uint64_t>(length - i, sizeof(buffer));
    if (!input.read(reinterpret_cast<char *>(buffer), (std::streamsize)count)) return {};
    state = fnv1a(buffer, count, state);
    i += count;
  }
  if (input.peek() != std::char_traits<char>::eof() || input.bad()) return {};
  return strprintf("file:%016llx:%llu", (unsigned long long)state, (unsigned long long)length);
}

bool source_entry_supported(const std::string &virtual_path) {
  const std::string ext = extension(virtual_key(virtual_path));
  return ext == ".mdl" || ext == ".bsp" || ext == ".vmdl_c" || ext == ".vmap_c" || ext == ".vwrld_c";
}

bool SourceContent::Mount::load_vpk(ArchiveBudget &budget, std::string &error) {
  kind = Vpk;
  const uint64_t length = parent ? container->vpk_length() : disk_size(local);
  std::vector<uint8_t> header;
  if (!read_archive_range(kVpkInline, 0, 12, header) || le32(header.data()) != 0x55aa1234)
    return fail(error, "Invalid VPK directory: " + label);
  const uint32_t version = le32(header.data() + 4);
  const uint32_t tree_size = le32(header.data() + 8);
  if (version != 1 && version != 2) return fail(error, "Unsupported VPK version " + std::to_string(version) + ": " + label);
  const size_t header_size = version == 1 ? 12 : 28;
  const uint64_t data_start = header_size + uint64_t(tree_size);
  if (tree_size > kMaxIndex || !fits(0, data_start, length)) return fail(error, "Invalid VPK directory size: " + label);
  uint64_t data_size = length - data_start;
  if (version == 2) {
    if (!read_archive_range(kVpkInline, 12, 16, header)) return fail(error, "Incomplete VPK header: " + label);
    data_size = le32(header.data() + 12);
    const uint64_t sections = data_size + le32(header.data() + 16) + le32(header.data() + 20) + le32(header.data() + 24);
    if (!fits(data_start, sections, length)) return fail(error, "Invalid VPK section sizes: " + label);
  }
  if (tree_size > kMaxArchiveBytes - budget.bytes) return fail(error, "Source VPK index budget exceeded: " + label);
  budget.bytes += tree_size;
  if (!read_archive_range(kVpkInline, header_size, tree_size, index)) return fail(error, "Cannot read VPK directory: " + label);

  size_t cursor = 0, count = 0;
  std::string ext, dir, name;
  while (tree_string(index, cursor, ext)) {
    if (ext.empty() && cursor == index.size()) return true;
    if (ext.empty() || ext.find_first_of("/\\") != std::string::npos) break;
    while (tree_string(index, cursor, dir) && !dir.empty()) {
      while (tree_string(index, cursor, name) && !name.empty()) {
        if (budget.entries == kMaxArchiveEntries) return fail(error, "Source VPK entry budget exceeded: " + label);
        ++budget.entries;
        if (++count > kMaxFiles || !fits(cursor, 18, index.size()) || name.find_first_of("/\\") != std::string::npos)
          return fail(error, "Invalid VPK directory entry: " + label);
        const uint8_t *p = index.data() + cursor;
        MountedFile f;
        f.crc = le32(p);
        f.preload_size = le16(p + 4);
        f.chunk = le16(p + 6);
        f.offset = le32(p + 8);
        f.size = le32(p + 12);
        cursor += 18;
        if (le16(p + 16) != 0xffff || !fits(cursor, f.preload_size, index.size()) ||
            (f.size && f.chunk == kVpkInline && !fits(f.offset, f.size, data_size)))
          return fail(error, "Invalid VPK entry bounds: " + label);
        f.preload_offset = (uint32_t)cursor;
        cursor += f.preload_size;
        if (f.chunk == kVpkInline) f.offset += data_start;
        std::string key = virtual_key((dir == " " ? "" : dir + "/") + name + (ext == " " ? "" : "." + ext));
        if (key.empty()) return fail(error, "Invalid virtual path in VPK directory: " + label);
        if (key.size() > kMaxArchiveBytes - budget.bytes) return fail(error, "Source VPK index budget exceeded: " + label);
        budget.bytes += key.size();
        const size_t key_size = key.size();
        if (files.emplace(std::move(key), std::move(f)).second) lookup_bytes += key_size;
      }
      if (!name.empty()) break;
    }
    if (!dir.empty()) break;
  }
  return fail(error, "Incomplete VPK directory tree: " + label);
}

bool SourceContent::Mount::load_zip(const std::vector<uint8_t> &zip, std::string &error) {
  kind = Zip;
  if (zip.size() < 22 || zip.size() > kMaxResource) return fail(error, "Invalid embedded ZIP size: " + label);
  const size_t limit = zip.size() > 22 + 65535 ? zip.size() - 22 - 65535 : 0;
  size_t end = zip.size() - 22;
  for (;;) {
    if (le32(zip.data() + end) == 0x06054b50 && le16(zip.data() + end + 20) == zip.size() - end - 22) break;
    if (end == limit) return fail(error, "Missing embedded ZIP directory: " + label);
    --end;
  }
  const uint8_t *tail = zip.data() + end;
  const uint16_t count = le16(tail + 10);
  const uint32_t dir_size = le32(tail + 12), dir_start = le32(tail + 16);
  if (le16(tail + 4) || le16(tail + 6) || le16(tail + 8) != count || count == 0xffff ||
      !fits(dir_start, dir_size, end))
    return fail(error, "Unsupported embedded ZIP directory: " + label);

  size_t cursor = dir_start, dir_end = cursor + dir_size;
  for (uint16_t i = 0; i < count; ++i) {
    if (!fits(cursor, 46, dir_end) || le32(zip.data() + cursor) != 0x02014b50)
      return fail(error, "Invalid embedded ZIP directory entry: " + label);
    const uint8_t *p = zip.data() + cursor;
    const uint16_t name_size = le16(p + 28), extra_size = le16(p + 30), comment_size = le16(p + 32);
    const size_t record_size = 46 + size_t(name_size) + extra_size + comment_size;
    if (!name_size || name_size > kMaxPath || !fits(cursor, record_size, dir_end) || le16(p + 34))
      return fail(error, "Invalid embedded ZIP directory bounds: " + label);
    cursor += record_size;
    const std::string name(reinterpret_cast<const char *>(p + 46), name_size);
    if (name.back() == '/') continue;
    std::string key = virtual_key(name);
    if (key.empty()) return fail(error, "Invalid virtual path in embedded ZIP: " + label);

    MountedFile f;
    const uint16_t flags = le16(p + 8);
    f.method = le16(p + 10);
    f.crc = le32(p + 16);
    f.size = le32(p + 20);
    f.unpacked_size = le32(p + 24);
    const uint32_t offset = le32(p + 42);
    if (!fits(offset, 30, dir_start) || le32(zip.data() + offset) != 0x04034b50)
      return fail(error, "Invalid embedded ZIP local header: " + label);
    const uint8_t *local_header = zip.data() + offset;
    f.offset = uint64_t(offset) + 30 + le16(local_header + 26) + le16(local_header + 28);
    if (le16(local_header + 6) != flags || le16(local_header + 8) != f.method || le16(local_header + 26) != name_size ||
        !fits(f.offset, f.size, dir_start) ||
        !std::equal(name.begin(), name.end(), reinterpret_cast<const char *>(local_header + 30)))
      return fail(error, "Invalid embedded ZIP payload bounds: " + label);
    if (flags & 1) f.method = UINT16_MAX;  // the read reports an encrypted entry as unsupported
    files.emplace(std::move(key), std::move(f));
  }
  if (cursor != dir_end) return fail(error, "Invalid embedded ZIP directory size: " + label);
  index = zip;
  return true;
}

bool SourceContent::Mount::read(const MountedFile &f, std::vector<uint8_t> &bytes) const {
  if (kind == Loose) {
    std::error_code ec;
    const disk::path current = disk::canonical(f.local, ec);
    const uint64_t size = disk_size(current);
    return !ec && current == f.local && beneath(local, current) && size <= kMaxResource &&
           read_range(current, 0, (size_t)size, bytes);
  }
  if (f.size > kMaxResource || f.unpacked_size > kMaxResource) return false;
  if (kind == Zip) {
    const uint8_t *data = index.data() + f.offset;
    if (f.method == 0 && f.size == f.unpacked_size) bytes.assign(data, data + f.size);
    else if (f.method != 8 || !zlib_inflate(data, f.size, bytes, false, f.unpacked_size)) return false;
    return bytes.size() == f.unpacked_size && checksum(bytes) == f.crc;
  }
  return f.vpk_length() <= kMaxResource && read_vpk_range(f, 0, (size_t)f.vpk_length(), bytes) && checksum(bytes) == f.crc;
}

bool SourceContent::Mount::read_archive_range(uint16_t chunk, uint64_t offset, size_t size, std::vector<uint8_t> &bytes) const {
  if (parent) {
    if (chunk == kVpkInline) return parent->read_vpk_range(*container, offset, size, bytes);
    const std::string path = chunk_name(package_path, chunk);
    const auto entry = parent->files.find(path);
    return entry != parent->files.end() && parent->read_vpk_range(entry->second, offset, size, bytes);
  }

  disk::path file = local;
  if (chunk != kVpkInline) {
    const std::string name = chunk_name(local.filename().u8string(), chunk);
    if (name.empty()) return false;
    file = local.parent_path() / disk::u8path(name);
  }
  std::error_code ec;
  return disk::canonical(file, ec) == file && !ec && read_range(file, offset, size, bytes);
}

bool SourceContent::Mount::read_vpk_range(const MountedFile &f, uint64_t offset, size_t size, std::vector<uint8_t> &bytes) const {
  if (!fits(offset, size, f.vpk_length())) return false;
  if (offset < f.preload_size) {
    const size_t take = std::min<size_t>(f.preload_size - (size_t)offset, size);
    const auto first = index.begin() + f.preload_offset + (size_t)offset;
    bytes.insert(bytes.end(), first, first + take);
    size -= take;
    offset += take;
  }
  // The enclosing archive's CRC covers its entire payload. Its directory is range-checked,
  // and only the requested inner resource is read and checked against its own CRC.
  return size == 0 || read_archive_range(f.chunk, f.offset + offset - f.preload_size, size, bytes);
}

void SourceContent::Mount::discover_children(const std::shared_ptr<const Mount> &outer, int depth, ArchiveBudget &budget,
                                             std::vector<std::shared_ptr<const Mount>> &children, std::vector<std::string> &warnings) {
  const auto warn = [&](const std::string &message) {
    if (warnings.size() < kMaxMountWarnings) warnings.push_back(message);
  };
  for (const auto &[path, file] : outer->files) {
    if (extension(path) != ".vpk") continue;
    const std::string directory = numbered_vpk_index(path.substr(0, path.size() - 4), ".vpk");
    if (!directory.empty() && outer->files.count(directory)) continue;
    if (budget.nested >= kMaxNestedPackages) {
      warn("Nested VPK count limit reached in " + outer->label + ". Select a smaller package.");
      return;
    }
    ++budget.nested;
    const std::string label = outer->label + " > " + path;
    if (depth >= kMaxNestedDepth) {
      warn("Nested VPK depth limit reached at " + label + ". Select the inner package directly.");
      continue;
    }
    auto child = std::make_shared<Mount>();
    child->parent = outer;
    child->package_path = path;
    child->container = &file;
    child->label = label;
    std::string error;
    ArchiveBudget candidate_budget = budget;
    if (!child->load_vpk(candidate_budget, error)) {
      warn(error + ". Select an intact VPK v1 or v2 package.");
      continue;
    }
    budget = candidate_budget;
    children.push_back(child);
    discover_children(child, depth + 1, budget, children, warnings);
  }
}

bool SourceContent::mount(const std::string &local_path, std::string &error) {
  std::string selected;
  return mount(local_path, error, selected);
}

bool SourceContent::mount(const std::string &local_path, std::string &error, std::string &selected_virtual_path) {
  selected_virtual_path.clear();
  error.clear();
  if (local_path.empty() || local_path.find_first_of("\r\n") != std::string::npos || local_path.find('\0') != std::string::npos)
    return fail(error, "Choose a Workshop or game content folder, a model/map file, or a VPK.");
  std::error_code ec;
  disk::path chosen = disk::canonical(disk::u8path(local_path), ec);
  if (ec) return fail(error, "Cannot open Source content: " + local_path);

  std::vector<std::shared_ptr<const Mount>> added, children;
  std::vector<std::string> warnings;
  ArchiveBudget budget;
  auto count_mounted = [&](const auto &mounts) {
    for (const auto &mount : mounts) {
      if (mount->kind != Mount::Vpk) continue;
      budget.bytes += mount->index.size() + mount->lookup_bytes;
      budget.entries += mount->files.size();
    }
  };
  count_mounted(mounts_);
  count_mounted(nested_mounts_);
  budget.nested = nested_mounts_.size();

  auto archive = [&](disk::path file) {
    for (const auto &mount : mounts_)
      if (mount->kind == Mount::Vpk && mount->local == file) return true;
    std::error_code file_error;
    if (disk::canonical(file, file_error) != file || file_error)
      return fail(error, "Cannot open VPK without leaving its content root: " + file.generic_u8string());
    auto pak = std::make_shared<Mount>();
    pak->local = std::move(file);
    pak->label = pak->local.generic_u8string();
    ArchiveBudget candidate_budget = budget;
    if (!pak->load_vpk(candidate_budget, error)) return false;
    budget = candidate_budget;
    added.push_back(pak);
    Mount::discover_children(pak, 0, budget, children, warnings);
    return true;
  };

  const bool folder = disk::is_directory(chosen, ec);
  const std::string ext = to_lower(chosen.extension().u8string());
  if (!folder && ext == ".vpk") {
    const disk::path dir = numbered_vpk_index(chosen);
    // A numbered chunk can begin with another VPK. Its sibling directory takes precedence.
    if (!dir.empty() && (vpk_directory_exists(dir) || !vpk_signature(chosen))) {
      const disk::path index = disk::canonical(dir, ec);
      if (ec || index.parent_path() != chosen.parent_path())
        return fail(error, "VPK chunk selected. Locate its directory VPK: " + dir.generic_u8string());
      chosen = index;
    }
    if (!archive(chosen)) return false;
  }
  else {
    auto loose = std::make_shared<Mount>();
    loose->local = chosen;
    if (!folder) {
      if (!source_entry_supported(chosen.filename().u8string()))
        return fail(error, "Unsupported Source entry: " + chosen.generic_u8string());
      loose->local = chosen.parent_path();
      for (disk::path parent = loose->local; parent != parent.root_path(); parent = parent.parent_path()) {
        const std::string name = to_lower(parent.filename().u8string());
        if (name == "models" || name == "maps") {
          loose->local = parent.parent_path();
          break;
        }
      }
    }
    loose->label = loose->local.generic_u8string();

    std::vector<disk::path> packages;
    size_t count = 0, path_bytes = 0;
    for (disk::recursive_directory_iterator it(loose->local, ec), end; !ec && it != end; it.increment(ec)) {
      const disk::path &file = it->path();
      const auto status = it->symlink_status(ec);
      if (ec) break;
      if (++count > kMaxFiles) return fail(error, "Too many files in Source content folder: " + loose->label);
      if (disk::is_symlink(status) || it.depth() >= 24) {
        it.disable_recursion_pending();
        if (disk::is_symlink(status)) continue;
      }
      if (disk::is_directory(status)) {
        // Canonical comparison also excludes junctions that directory_iteration may follow on Windows.
        if (disk::canonical(file, ec) != file) it.disable_recursion_pending();
        if (ec) break;
        continue;
      }
      if (!disk::is_regular_file(status)) continue;
      std::string key = virtual_key(file.lexically_relative(loose->local).generic_u8string());
      if (key.empty()) continue;
      path_bytes += key.size();
      if (path_bytes > kMaxIndex) return fail(error, "Source content folder index is too large: " + loose->label);
      if (extension(key) == ".vpk" && !vpk_directory_exists(numbered_vpk_index(file))) packages.push_back(file);
      MountedFile record;
      record.local = file;
      const auto inserted = loose->files.emplace(std::move(key), record);
      if (!inserted.second && (file == chosen || (inserted.first->second.local != chosen && file < inserted.first->second.local)))
        inserted.first->second = std::move(record);
    }
    if (ec) return fail(error, "Cannot scan Source content folder: " + loose->label + " (" + ec.message() + ")");
    const std::string selected = folder ? std::string{} : virtual_key(chosen.lexically_relative(loose->local).generic_u8string());
    if (!folder && (selected.empty() || !loose->files.count(selected)))
      return fail(error, "Selected Source file is outside the content index: " + chosen.generic_u8string());
    added.push_back(loose);
    std::sort(packages.begin(), packages.end());
    const size_t package_count = std::min(packages.size(), kMaxDiscoveredPackages);
    for (size_t i = 0; i < package_count; ++i) {
      if (archive(packages[i])) continue;
      if (warnings.size() < kMaxMountWarnings) warnings.push_back(error + ". Select an intact VPK v1 or v2 package.");
      error.clear();
    }
    if (packages.size() > package_count && warnings.size() < kMaxMountWarnings)
      warnings.push_back("VPK discovery limit reached in " + loose->label + ". Select a smaller folder or a VPK directly.");
    selected_virtual_path = selected;
  }

  for (auto &m : added) {
    const auto same = [&](const auto &old) { return old->kind == m->kind && old->local == m->local; };
    if (std::none_of(mounts_.begin(), mounts_.end(), same)) mounts_.push_back(std::move(m));
  }
  nested_mounts_.insert(nested_mounts_.end(), children.begin(), children.end());
  warnings.resize(std::min(warnings.size(), kMaxMountWarnings - mount_warnings_.size()));
  mount_warnings_.insert(mount_warnings_.end(), warnings.begin(), warnings.end());
  return true;
}

bool SourceContent::mount_embedded_zip(const std::string &label, const std::vector<uint8_t> &zip, std::string &error) {
  error.clear();
  auto embedded = std::make_shared<Mount>();
  embedded->label = label;
  if (!embedded->load_zip(zip, error)) return false;
  mounts_.insert(mounts_.begin(), std::move(embedded));
  return true;
}

bool SourceContent::locate(const std::string &virtual_path, SourceEntry &entry, std::string &error) const {
  entry = {};
  error.clear();
  const std::string key = virtual_key(virtual_path);
  if (key.empty()) return fail(error, "Invalid Source virtual path: " + virtual_path);
  for (const auto &mount : mounts_) {
    const auto it = mount->files.find(key);
    if (it == mount->files.end()) continue;
    entry = {it->first, mount->kind == Mount::Loose ? it->second.local.generic_u8string() : mount->label};
    return true;
  }
  return fail(error, "Missing Source dependency: " + key);
}

bool SourceContent::read(const std::string &virtual_path, std::vector<uint8_t> &bytes, std::string &error) const {
  bytes.clear();
  error.clear();
  const std::string key = virtual_key(virtual_path);
  if (key.empty()) return fail(error, "Invalid Source virtual path: " + virtual_path);
  for (const auto &mount : mounts_) {
    const auto it = mount->files.find(key);
    if (it == mount->files.end()) continue;
    if (mount->read(it->second, bytes)) return true;
    bytes.clear();
    return fail(error, "Cannot read Source resource " + key + " from " + mount->label +
                           " (missing data, invalid archive, or unsupported encoding)");
  }
  return fail(error, "Missing Source dependency: " + key);
}

std::vector<SourceEntry> SourceContent::entries() const {
  std::map<std::string, std::string> supported;
  for (const auto &mount : mounts_)
    for (const auto &file : mount->files)
      if (source_entry_supported(file.first))
        supported.emplace(file.first, mount->kind == Mount::Loose ? file.second.local.generic_u8string() : mount->label);
  std::vector<SourceEntry> result;
  result.reserve(supported.size());
  for (auto &file : supported) result.push_back({file.first, std::move(file.second)});
  for (const auto &mount : nested_mounts_)
    for (const auto &file : mount->files)
      if (source_entry_supported(file.first)) result.push_back({file.first, mount->label});
  std::sort(result.begin(), result.end(), [](const SourceEntry &a, const SourceEntry &b) {
    return a.virtual_path < b.virtual_path || (a.virtual_path == b.virtual_path && a.provider < b.provider);
  });
  return result;
}

bool SourceContent::for_entry(const SourceEntry &entry, SourceContent &scoped, std::string &error) const {
  error.clear();
  const std::string key = virtual_key(entry.virtual_path);
  for (const auto &mount : nested_mounts_) {
    if (mount->label != entry.provider || !mount->files.count(key)) continue;
    SourceContent result;
    for (const auto &current : mounts_)
      if (current->kind == Mount::Zip) result.mounts_.push_back(current);
    for (auto ancestor = mount; ancestor; ancestor = ancestor->parent) result.mounts_.push_back(ancestor);
    for (const auto &current : mounts_)
      if (current->kind != Mount::Zip && std::find(result.mounts_.begin(), result.mounts_.end(), current) == result.mounts_.end())
        result.mounts_.push_back(current);
    scoped = std::move(result);
    return true;
  }
  SourceEntry current;
  if (locate(entry.virtual_path, current, error) && current.provider == entry.provider) {
    scoped = *this;
    return true;
  }
  return fail(error, "Source entry " + entry.virtual_path + " is unavailable from " + entry.provider);
}

std::string SourceContent::entry_fingerprint(const SourceEntry &entry) const {
  SourceContent scoped;
  std::string error;
  if (!for_entry(entry, scoped, error)) return {};
  const std::string key = virtual_key(entry.virtual_path);
  for (const auto &mount : scoped.mounts_) {
    const auto found = mount->files.find(key);
    if (found == mount->files.end()) continue;
    const auto &file = found->second;
    if (mount->kind == Mount::Loose) {
      std::error_code ec;
      const auto current = disk::canonical(file.local, ec);
      return !ec && current == file.local && beneath(mount->local, current)
               ? source_loose_fingerprint(current.generic_u8string())
               : std::string{};
    }
    const uint64_t length = mount->kind == Mount::Vpk ? file.vpk_length() : file.unpacked_size;
    return strprintf("package:%08x:%llu", unsigned(file.crc), (unsigned long long)length);
  }
  return {};
}

static bool dispatch_source_entry(const SourceContent &content, const SourceEntry &entry, ImportResult &out) {
  const std::string ext = extension(entry.virtual_path);
  (void)ext;  // some readers arrive in subsequent rounds of this pass
#if __has_include("source1_model.cpp")
  if (ext == ".mdl") return import_source1_model(content, entry.virtual_path, out);
#endif
#if __has_include("source1_map.cpp")
  if (ext == ".bsp") return import_source1_map(content, entry.virtual_path, out);
#endif
#if __has_include("source2_model.cpp")
  if (ext == ".vmdl_c") return import_source2_model(content, entry.virtual_path, out);
#endif
#if __has_include("source2_map.cpp")
  if (ext == ".vmap_c" || ext == ".vwrld_c") return import_source2_map(content, entry.virtual_path, out);
#endif
  return fail(out.error, "No Source reader is available for " + entry.virtual_path + " in this build.");
}

bool import_source_entry(const SourceContent &content, const SourceEntry &entry, ImportResult &out) {
  SourceContent scoped;
  return content.for_entry(entry, scoped, out.error) &&
         dispatch_source_entry(scoped, {virtual_key(entry.virtual_path), entry.provider}, out);
}

bool import_source_entry(const SourceContent &content, const std::string &virtual_path, ImportResult &out) {
  SourceEntry entry;
  if (content.locate(virtual_path, entry, out.error)) return dispatch_source_entry(content, entry, out);
  const std::string key = virtual_key(virtual_path);
  if (key.empty()) return false;
  for (const auto &candidate : content.entries()) {
    if (candidate.virtual_path != key) continue;
    if (!entry.provider.empty())
      return fail(out.error, "Several nested VPKs provide " + key + ". Select its package in Source Engine Assets.");
    entry = candidate;
  }
  return !entry.provider.empty() && import_source_entry(content, entry, out);
}

}  // namespace bl

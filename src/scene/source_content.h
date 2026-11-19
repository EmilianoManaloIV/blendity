// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bl {

struct ImportResult;

struct SourceEntry {
  std::string virtual_path;
  std::string provider;  // local file or archive chain, including nested package names
};

class SourceContent {
 public:
  // Explicit roots are searched in the order in which they were added.
  bool mount(const std::string &local_path, std::string &error);
  // When the local path names a loose model/map, also return its selected virtual path.
  bool mount(const std::string &local_path, std::string &error, std::string &selected_virtual_path);
  // Call on a copy for a particular map. Embedded contents take precedence.
  bool mount_embedded_zip(const std::string &label, const std::vector<uint8_t> &zip, std::string &error);

  bool read(const std::string &virtual_path, std::vector<uint8_t> &bytes, std::string &error) const;
  bool locate(const std::string &virtual_path, SourceEntry &entry, std::string &error) const;
  // Explicit mounts contribute their first provider per path. Each nested package lists its own entries.
  std::vector<SourceEntry> entries() const;
  // The selected package, its ancestors, and the explicit mounts in order. Siblings are outside this scope.
  bool for_entry(const SourceEntry &entry, SourceContent &scoped, std::string &error) const;
  const std::vector<std::string> &mount_warnings() const { return mount_warnings_; }

 private:
  struct Mount;
  std::vector<std::shared_ptr<const Mount>> mounts_, nested_mounts_;
  std::vector<std::string> mount_warnings_;
};

bool source_entry_supported(const std::string &virtual_path);
bool import_source_entry(const SourceContent &content, const std::string &virtual_path, ImportResult &out);
bool import_source_entry(const SourceContent &content, const SourceEntry &entry, ImportResult &out);
bool import_source1_model(const SourceContent &content, const std::string &virtual_path, ImportResult &out);
bool import_source1_map(const SourceContent &content, const std::string &virtual_path, ImportResult &out);
bool import_source2_model(const SourceContent &content, const std::string &virtual_path, ImportResult &out);
bool import_source2_map(const SourceContent &content, const std::string &virtual_path, ImportResult &out);

}  // namespace bl

// SPDX-License-Identifier: GPL-2.0-or-later
// Linux measurement for the production streamed PLY importer, with a deterministic scratch fixture.
#include "../../src/core/core.h"
#include "../../src/core/hash.h"
#include "../../src/core/jobs.h"
#include "../../src/scene/import.h"
#include "../../src/scene/import_splat.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <vector>

namespace {

using namespace bl;

constexpr int kMinRecords = 100000;
constexpr int kMaxRecords = 1000000;
constexpr size_t kRest = 15;
constexpr int kDegree = 3;
constexpr size_t kDropped = 3;
constexpr size_t kSh = 14;
constexpr const char *kProperties[kSh] = {"x", "y", "z", "f_dc_0", "f_dc_1", "f_dc_2", "opacity", "scale_0", "scale_1",
                                          "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"};
using Record = std::array<float, kSh + 3 * kRest>;
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559 && sizeof(Record) == 236);
static_assert(sizeof(Vec3) == 3 * sizeof(float) && sizeof(Quat) == 4 * sizeof(float));

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

int parse_bound(std::string_view input, int low, int high, const char *message) {
  int value = 0;
  const char *end = input.data() + input.size();
  const auto res = std::from_chars(input.data(), end, value);
  require(res.ec == std::errc() && res.ptr == end && value >= low && value <= high, message);
  return value;
}

std::string header(size_t count) {
  std::string text = "ply\nformat binary_little_endian 1.0\nelement vertex " + std::to_string(count) + "\n";
  for (const char *property : kProperties) text += std::string("property float ") + property + "\n";
  for (size_t i = 0; i < 3 * kRest; i++) text += "property float f_rest_" + std::to_string(i) + "\n";
  return text + "end_header\n";
}

bool invalid_position(size_t index, size_t count) { return index == 0 || index == count / 2 || index == count - 1; }

Vec3 fixture_position(size_t i) { return {(float)(i + 1) / 1024.0f, -(float)(i % 31 + 1) / 8, (float)(i % 97 + 1) / 16}; }

Record fixture_record(size_t i, size_t count) {
  Vec3 p = fixture_position(i);
  if (invalid_position(i, count)) {
    constexpr uint32_t quiet_nan = 0x7fc00000u;
    std::memcpy(&p.x, &quiet_nan, sizeof(p.x));
  }
  Record row = {p.x, p.y, p.z, (float)(i % 7) / 32, -0.25f, 0.125f, (float)((int)(i % 17) - 8) / 4,
                -4.0f + (float)(i % 7) / 8, -3.0f, -2.0f, 2.0f, 0.0f, 0.0f, 0.0f};
  for (size_t j = 0; j < 3 * kRest; j++) row[kSh + j] = (float)((int)((i + j * 7) % 31) - 15) / 1024;
  return row;
}

void generate(const std::string &path, size_t count) {
  const std::string text = header(count);
  FILE *file = std::fopen(path.c_str(), "wbx");
  require(file != nullptr, "cannot create a new fixture file");
  bool wrote = std::fwrite(text.data(), 1, text.size(), file) == text.size();
  std::array<Record, 1024> rows;
  for (size_t first = 0; wrote && first < count; first += rows.size()) {
    const size_t batch = std::min(rows.size(), count - first);
    for (size_t j = 0; j < batch; j++) rows[j] = fixture_record(first + j, count);
    wrote = std::fwrite(rows.data(), sizeof(Record), batch, file) == batch;
  }
  const int closed = std::fclose(file);
  require(wrote && closed == 0, "could not write the complete fixture");
  std::printf("{\"records\":%zu,\"input_bytes\":%zu,\"record_bytes\":%zu,\"invalid_positions\":%zu}\n",
              count, text.size() + count * sizeof(Record), sizeof(Record), kDropped);
}

bool near(float actual, float expected) { return std::abs(actual - expected) <= 2e-6f; }

bool near(Vec3 actual, Vec3 expected) { return near(actual.x, expected.x) && near(actual.y, expected.y) && near(actual.z, expected.z); }

void check_attributes(const PointCloud &cloud, size_t retained, size_t source, size_t count) {
  const Record row = fixture_record(source, count);
  const Vec3 dc = Vec3(0.5f) + kSplatShC0 * Vec3(row[3], row[4], row[5]);
  require(near(cloud.color[retained], dc) && near(cloud.opacity[retained], 1.0f / (1.0f + std::exp(-row[6]))),
          "DC color or opacity disagrees with the fixture");
  const Vec3 scale(std::exp(row[7]), std::exp(row[8]), std::exp(row[9]));
  require(near(cloud.scale[retained], scale), "scale disagrees with the fixture");
  const Quat q = cloud.rotation[retained];
  require(near(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1.0f), "unnormalized splat rotation");
  float covariance[6];
  splat_covariance(cloud, retained, covariance);
  const float expected[] = {scale.x * scale.x, 0, 0, scale.z * scale.z, 0, scale.y * scale.y};
  for (size_t i = 0; i < 6; i++) require(near(covariance[i], expected[i]), "splat rotation disagrees with file-to-editor axes");

  // Evaluate the file's SH in the inverse-transformed view direction to check decoding and axes together.
  for (Vec3 direction : {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), normalize(Vec3(1, 2, 3)), normalize(Vec3(-3, 2, -1))}) {
    float basis[kRest + 1];
    splat_sh_basis({-direction.x, -direction.z, direction.y}, basis);
    Vec3 expected_color = dc;
    for (size_t k = 0; k < kRest; k++) {
      const Vec3 coefficient(row[kSh + k], row[kSh + k + kRest], row[kSh + k + 2 * kRest]);
      expected_color += coefficient * basis[k + 1];
    }
    require(near(splat_color(cloud, retained, direction), expected_color), "spherical harmonics disagree with fixture");
  }
}

size_t check_cloud(const ImportResult &result, const std::string &path, size_t count) {
  const std::string name = std::filesystem::path(path).stem().string();
  require(result.source == path && result.error.empty() && result.nodes.size() == 1 && result.nodes[0].name == name &&
              result.nodes[0].points && !result.nodes[0].mesh, "expected one named splat cloud from the given file");
  require(result.notes == std::vector<std::string>{"PLY: 3 points with no position (NaN) left out"}, "missing invalid-position diagnostic");
  const PointCloud &cloud = *result.nodes[0].points;
  require(cloud.consistent() && cloud.is_splats() && cloud.sh_degree == kDegree && cloud.count() == count - kDropped,
          "splat array layout, degree, or retained count is wrong");
  size_t checked = 0, retained = 0;
  for (size_t i = 0; i < count; i++) {
    if (invalid_position(i, count)) continue;
    const Vec3 p = fixture_position(i);
    require(cloud.position[retained] == Vec3(-p.x, p.z, -p.y), "wrong position, source order, or invalid-record compaction");
    if (i == 1 || i == count / 4 || i == count / 2 - 1 || i == count / 2 + 1 || i == 3 * count / 4 || i == count - 2) {
      check_attributes(cloud, retained, i, count);
      checked++;
    }
    retained++;
  }
  return checked;
}

uint64_t cloud_digest(const PointCloud &cloud) {
  uint64_t digest = 0;
  auto add = [&](const auto &values) { digest = xxh64(values.data(), values.size() * sizeof(values[0]), digest); };
  add(cloud.position), add(cloud.color), add(cloud.opacity), add(cloud.scale), add(cloud.rotation), add(cloud.sh_rest);
  return digest;
}

struct Sample {
  double ms = 0.0;
  DiskPlyTimes disk;
};

void measure(const std::string &path, size_t count, int repeats) {
  const size_t payload = count * sizeof(Record);
  const uintmax_t file_bytes = std::filesystem::file_size(path);
  require(file_bytes == header(count).size() + payload, "file size disagrees with the fixture record count");
  JobSystem &jobs = JobSystem::global();
  const int threads = jobs.thread_count();
  uint64_t digest = 0;
  size_t checked = 0;
  std::vector<Sample> samples;
  for (int row = 0; row < repeats; row++) {
    std::string fallback;
    ImportResult result;
    Sample sample;
    std::optional<bool> ok;
    // Editor::import_async also uses the background pool. The diagnostic's caller plays the import worker here.
    jobs.run_background([&] {
      const double start = now_seconds();
      ok = try_import_binary_ply_point_cloud(path, fallback, result, &sample.disk);
      sample.ms = (now_seconds() - start) * 1000.0;
    });
    require(ok.has_value() && fallback.empty(), "fixture did not take the production streamed PLY path");
    if (!*ok) throw std::runtime_error("streamed import failed: " + result.error);
    const DiskPlyTimes &disk = sample.disk;
    const SplatReadTimes &t = disk.phases;
    require(disk.read_batches > 1 && disk.read_bytes == payload, "stream measurements do not cover the full vertex payload");
    const double sum = t.setup_ms + disk.read_ms + t.decode_ms + t.axes_ms + t.validate_ms;
    require(std::isfinite(sum) && sum <= sample.ms && std::min({t.setup_ms, disk.read_ms, t.decode_ms, t.axes_ms, t.validate_ms}) >= 0,
            "phase timers overlap or fall outside the import duration");
    checked = check_cloud(result, path, count);
    const uint64_t this_digest = cloud_digest(*result.nodes[0].points);
    require(row == 0 || this_digest == digest, "decoded attribute digest changed between repeats");
    digest = this_digest;
    samples.push_back(sample);
  }
  struct rusage usage{};
  require(getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss > 0, "could not read process maximum RSS");
  std::printf("{\n  \"input_bytes\": %llu, \"records\": %zu, \"kept\": %zu, \"dropped\": %zu, \"sh_degree\": %d,\n"
              "  \"threads\": %d, \"background_lanes\": %d, \"peak_rss_kib\": %ld, \"cloud_digest\": \"%016llx\",\n"
              "  \"checks\": {\"positions_per_import\": %zu, \"attribute_records_per_import\": %zu, \"digest_comparisons\": %d},\n"
              "  \"samples\": [\n",
              (unsigned long long)file_bytes, count, count - kDropped, kDropped, kDegree, threads, std::max(1, threads - 2),
              usage.ru_maxrss, (unsigned long long)digest, count - kDropped, checked, repeats - 1);
  for (size_t i = 0; i < samples.size(); i++) {
    const Sample &sample = samples[i];
    const DiskPlyTimes &disk = sample.disk;
    const SplatReadTimes &t = disk.phases;
    std::printf("    {\"total_ms\": %.6f, \"setup_ms\": %.6f, \"read_ms\": %.6f, \"read_bytes\": %llu, \"read_batches\": %zu, "
                "\"decode_ms\": %.6f, \"axes_ms\": %.6f, \"validate_ms\": %.6f}%s\n",
                sample.ms, t.setup_ms, disk.read_ms, (unsigned long long)disk.read_bytes, disk.read_batches, t.decode_ms, t.axes_ms,
                t.validate_ms, i + 1 == samples.size() ? "" : ",");
  }
  std::puts("  ]\n}");
}

}  // namespace

int main(int argc, char **argv) {
  const std::string_view mode = argc > 1 ? argv[1] : "";
  if (!((mode == "generate" && argc == 4) || (mode == "measure" && argc == 5))) {
    std::fputs("usage: ply_import_probe generate ABSOLUTE_NEW_PLY RECORDS\n"
               "       ply_import_probe measure ABSOLUTE_PLY RECORDS REPEATS\n", stderr);
    return 2;
  }
  try {
    const uint16_t endian = 1;
    require(*reinterpret_cast<const unsigned char *>(&endian) == 1, "the probe needs a little-endian Linux host");
    require(std::filesystem::path(argv[2]).is_absolute(), "fixture path must be absolute");
    const size_t count = (size_t)parse_bound(argv[3], kMinRecords, kMaxRecords, "records must be between 100000 and 1000000");
    if (mode == "generate") generate(argv[2], count);
    else measure(argv[2], count, parse_bound(argv[4], 2, 10, "repeats must be between 2 and 10"));
  }
  catch (const std::exception &error) {
    std::fprintf(stderr, "PLY import probe: %s\n", error.what());
    return 1;
  }
  return 0;
}

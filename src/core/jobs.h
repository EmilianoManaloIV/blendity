// SPDX-License-Identifier: GPL-2.0-or-later
// Fork/join parallel_for. Mirrors blender::threading::parallel_for
// (blender/source/blender/blenlib/BLI_task.hh): built with Blender's libraries
// it runs on oneTBB's work-stealing scheduler, exactly as Blender does, and
// shares TBB's threads with Embree, OpenImageDenoise and OpenSubdiv. Without
// TBB it uses the minimal thread pool below.
// Theory: Game Engine Architecture Vol. I, ch. 4 "Parallelism and Concurrent
// Programming" (4.3 Explicit Parallelism, 4.6 Thread Synchronization Primitives).
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace bl {

class JobSystem {
 public:
  enum class Backend { Builtin, TBB };

  /* threads == 0 -> hardware_concurrency - 1 workers (+ the calling thread). */
  explicit JobSystem(int threads = 0);
  ~JobSystem();
  JobSystem(const JobSystem &) = delete;
  JobSystem &operator=(const JobSystem &) = delete;

  /* Calls fn(begin, end) over [0, count) in chunks of at least `grain`, on
   * all threads, and returns once every chunk is done. With the built-in pool
   * nested calls run inline; with TBB they run in parallel too. */
  void parallel_for(int64_t count, int64_t grain, const std::function<void(int64_t, int64_t)> &fn);

  int thread_count() const;
  /* Restrict parallelism (1 = serial), used by profiler toggles & stress tests. */
  void set_max_threads(int n) { max_threads_ = n < 1 ? 1 : n; }
  int max_threads() const { return max_threads_; }

  /* TBB when built with it (the default), else the built-in pool. */
  static bool tbb_available();
  Backend backend() const { return backend_; }
  void set_backend(Backend b);
  const char *backend_name() const { return backend_ == Backend::TBB ? "oneTBB" : "built-in pool"; }

  static JobSystem &global();

 private:
  void start_workers();
  void worker_main();
  bool run_one_chunk();

  Backend backend_ = Backend::Builtin;
  int requested_threads_ = 0;
  std::once_flag workers_started_;
  struct TbbState;
  TbbState *tbb_ = nullptr;

  std::vector<std::thread> workers_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable done_;
  bool quit_ = false;
  uint64_t generation_ = 0;
  int max_threads_ = 1 << 30;

  /* Current job (one at a time; nested calls run inline). */
  const std::function<void(int64_t, int64_t)> *fn_ = nullptr;
  int64_t count_ = 0, grain_ = 1;
  std::atomic<int64_t> next_{0};
  std::atomic<int64_t> remaining_{0};
  std::atomic<int> active_workers_{0};
  int allowed_workers_ = 0;
  std::atomic<bool> busy_{false};
};

}  // namespace bl

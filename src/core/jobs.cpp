// SPDX-License-Identifier: GPL-2.0-or-later
#include "jobs.h"

#include <algorithm>
#include <memory>

#ifdef BL_WITH_TBB
#  include <oneapi/tbb/blocked_range.h>
#  include <oneapi/tbb/info.h>
#  include <oneapi/tbb/parallel_for.h>
#  include <oneapi/tbb/task_arena.h>
#endif

namespace bl {

struct JobSystem::TbbState {
#ifdef BL_WITH_TBB
  std::mutex mutex;
  std::unique_ptr<tbb::task_arena> arena;  // only when max_threads limits parallelism
  int arena_threads = 0;
#endif
};

JobSystem::JobSystem(int threads) : requested_threads_(threads) {
#ifdef BL_WITH_TBB
  tbb_ = new TbbState();
  backend_ = Backend::TBB;  // the built-in workers start only if someone switches back
#endif
}

JobSystem::~JobSystem() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
  }
  wake_.notify_all();
  for (auto &t : workers_) t.join();
  delete tbb_;
}

bool JobSystem::tbb_available() {
#ifdef BL_WITH_TBB
  return true;
#else
  return false;
#endif
}

void JobSystem::set_backend(Backend b) { backend_ = b == Backend::TBB && tbb_available() ? Backend::TBB : Backend::Builtin; }

int JobSystem::thread_count() const {
#ifdef BL_WITH_TBB
  if (backend_ == Backend::TBB) return tbb::info::default_concurrency();
#endif
  int threads = requested_threads_ > 0 ? requested_threads_ : (int)std::max(1u, std::thread::hardware_concurrency()) - 1;
  return threads + 1;
}

void JobSystem::start_workers() {
  std::call_once(workers_started_, [this] {
    int threads = thread_count() - 1;
    for (int i = 0; i < threads; i++) workers_.emplace_back([this] { worker_main(); });
  });
}

JobSystem &JobSystem::global() {
  static JobSystem js;
  return js;
}

bool JobSystem::run_one_chunk() {
  const auto *fn = fn_;
  int64_t count = count_, grain = grain_;
  int64_t begin = next_.fetch_add(grain);
  if (begin >= count) return false;
  int64_t end = std::min(count, begin + grain);
  (*fn)(begin, end);
  if (remaining_.fetch_sub(end - begin) - (end - begin) == 0) {
    std::lock_guard<std::mutex> lock(mutex_);
    done_.notify_all();
  }
  return true;
}

void JobSystem::worker_main() {
  uint64_t seen = 0;
  int index;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    static int counter = 0;
    index = counter++;
  }
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [&] { return quit_ || generation_ != seen; });
      if (quit_) return;
      seen = generation_;
      /* Job already finished (late wake-up) or this worker is throttled. */
      if (fn_ == nullptr || index >= allowed_workers_) continue;
      active_workers_++;
    }
    /* Parameters cannot change while active_workers_ > 0: parallel_for waits
     * for zero active workers both before setting up and before returning. */
    while (run_one_chunk()) {
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      active_workers_--;
      done_.notify_all();
    }
  }
}

void JobSystem::parallel_for(int64_t count, int64_t grain, const std::function<void(int64_t, int64_t)> &fn) {
  if (count <= 0) return;
  grain = std::max<int64_t>(1, grain);
#ifdef BL_WITH_TBB
  if (backend_ == Backend::TBB) {
    if (count <= grain || max_threads_ <= 1) {
      fn(0, count);
      return;
    }
    /* Same call Blender makes in BLI_task.hh: blocked_range with the grain as
     * the minimum chunk, TBB's auto partitioner and work stealing. */
    auto run = [&] {
      tbb::parallel_for(tbb::blocked_range<int64_t>(0, count, grain),
                        [&](const tbb::blocked_range<int64_t> &r) { fn(r.begin(), r.end()); });
    };
    if (max_threads_ >= tbb::info::default_concurrency()) {
      run();
      return;
    }
    tbb::task_arena *arena;
    {
      std::lock_guard<std::mutex> lock(tbb_->mutex);
      if (!tbb_->arena || tbb_->arena_threads != max_threads_) {
        tbb_->arena = std::make_unique<tbb::task_arena>(max_threads_);
        tbb_->arena_threads = max_threads_;
      }
      arena = tbb_->arena.get();
    }
    arena->execute(run);
    return;
  }
#endif
  start_workers();
  int allowed = std::min<int>((int)workers_.size(), max_threads_ - 1);
  /* Wake only as many workers as there are chunks beyond the caller's. */
  allowed = (int)std::min<int64_t>(allowed, (count + grain - 1) / grain - 1);
  bool expected = false;
  /* Serial path: small jobs, nested calls, or threading disabled. */
  if (count <= grain || allowed <= 0 || !busy_.compare_exchange_strong(expected, true)) {
    fn(0, count);
    return;
  }
  {
    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [&] { return active_workers_.load() == 0; });
    fn_ = &fn;
    count_ = count;
    grain_ = grain;
    next_ = 0;
    remaining_ = count;
    allowed_workers_ = allowed;
    generation_++;
  }
  wake_.notify_all();
  while (run_one_chunk()) {
  }
  {
    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [&] { return remaining_.load() == 0 && active_workers_.load() == 0; });
    fn_ = nullptr;
  }
  busy_ = false;
}

}  // namespace bl

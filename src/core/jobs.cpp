// SPDX-License-Identifier: GPL-2.0-or-later
#include "jobs.h"

#include <algorithm>
#include <cstdlib>
#include <memory>

#ifdef BL_WITH_TBB
#  include <oneapi/tbb/blocked_range.h>
#  include <oneapi/tbb/global_control.h>
#  include <oneapi/tbb/info.h>
#  include <oneapi/tbb/parallel_for.h>
#  include <oneapi/tbb/task_arena.h>
#endif

namespace bl {

namespace {
thread_local bool tl_background = false;  // inside run_background on the built-in pool (and on the background pool's workers): loops go to the background pool
}

struct JobSystem::TbbState {
#ifdef BL_WITH_TBB
  std::mutex mutex;
  std::shared_ptr<tbb::task_arena> arena;  // only when max_threads limits parallelism (shared: a loop still in it keeps it)
  int arena_threads = 0;
  std::unique_ptr<tbb::global_control> limit;  // the thread ceiling, for every TBB user in the process
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
  if (backend_ == Backend::TBB) {
    const int all = tbb::info::default_concurrency();
    const int limit = thread_limit_.load();
    return limit > 0 ? std::min(all, limit) : all;
  }
#endif
  int threads = requested_threads_ > 0 ? requested_threads_ : (int)std::max(1u, std::thread::hardware_concurrency()) - 1;
  const int limit = thread_limit_.load();
  if (limit > 0) threads = std::min(threads, limit - 1);
  return std::max(0, threads) + 1;
}

void JobSystem::set_thread_limit(int n) {
  thread_limit_ = std::max(0, n);
  {
    std::lock_guard<std::mutex> lock(background_mutex_);
    background_.reset();  // (sized under the old ceiling: the next run_background makes one under the new; a loop still in it keeps it)
  }
#ifdef BL_WITH_TBB
  std::lock_guard<std::mutex> lock(tbb_->mutex);
  tbb_->limit.reset();
  if (thread_limit_.load() > 0) tbb_->limit = std::make_unique<tbb::global_control>(tbb::global_control::max_allowed_parallelism, (size_t)thread_limit_.load());
  tbb_->arena.reset();  // an arena sized under the old ceiling
#endif
}

void JobSystem::start_workers() {
  std::call_once(workers_started_, [this] {
    int threads = thread_count() - 1;
    for (int i = 0; i < threads; i++) workers_.emplace_back([this] { worker_main(); });
  });
}

JobSystem &JobSystem::global() {
  static JobSystem js;
  static bool once = [] {
    if (const char *e = std::getenv("BLENDITY_THREADS"))
      if (std::atoi(e) > 0) js.set_thread_limit(std::atoi(e));
    return true;
  }();
  (void)once;
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
  if (is_background_) tl_background = true;  // (a nested loop from here stays on this pool: inline while it is busy)
  uint64_t seen = 0;
  int index;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    index = worker_counter_++;  // (round 412: this pool's own count - a static shared with the background pool had throttled every one of its workers)
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

bool JobSystem::in_background() { return tl_background; }

/* Round 412 (ADR 0106): the built-in backend's second pool, two threads short of the machine (as the TBB arena), made
 * when a run_background first asks and dropped when the thread ceiling changes. Its workers count as background threads,
 * so a nested loop from one of them comes back here and runs inline (the pool is busy), never on the main thread's pool. */
std::shared_ptr<JobSystem> JobSystem::background_pool() {
  std::lock_guard<std::mutex> lock(background_mutex_);
  if (!background_) {
    background_ = std::make_shared<JobSystem>(std::max(1, thread_count() - 3));  // (n workers + the background thread = the machine - 2)
    background_->set_backend(Backend::Builtin);
    background_->is_background_ = true;
  }
  return background_;
}

void JobSystem::run_background(const std::function<void()> &fn) {
  const bool was = tl_background;
  tl_background = true;  // (in_background says so on either backend)
#ifdef BL_WITH_TBB
  if (backend_ == Backend::TBB) {
    tbb::task_arena arena(std::max(1, thread_count() - 2));
    arena.execute(fn);
    tl_background = was;
    return;
  }
#endif
  fn();
  tl_background = was;
}

void JobSystem::parallel_for(int64_t count, int64_t grain, const std::function<void(int64_t, int64_t)> &fn) {
  if (count <= 0) return;
  grain = std::max<int64_t>(1, grain);
  if (tl_background && backend_ != Backend::TBB && !is_background_) {
    background_pool()->parallel_for(count, grain, fn);  // (round 412: the background pool's workers, not the main thread's)
    return;
  }
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
    if (max_threads_ >= thread_count()) {
      run();
      return;
    }
    std::shared_ptr<tbb::task_arena> arena;
    {
      std::lock_guard<std::mutex> lock(tbb_->mutex);
      if (!tbb_->arena || tbb_->arena_threads != max_threads_) {
        tbb_->arena = std::make_shared<tbb::task_arena>(std::min(max_threads_, thread_count()));
        tbb_->arena_threads = max_threads_;
      }
      arena = tbb_->arena;
    }
    arena->execute(run);
    return;
  }
#endif
  start_workers();
  int allowed = std::min<int>({(int)workers_.size(), max_threads_ - 1, thread_count() - 1});  // the ceiling holds after the workers started
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

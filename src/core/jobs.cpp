// SPDX-License-Identifier: GPL-2.0-or-later
#include "jobs.h"

#include <algorithm>

namespace bl {

JobSystem::JobSystem(int threads) {
  if (threads <= 0) threads = (int)std::max(1u, std::thread::hardware_concurrency()) - 1;
  for (int i = 0; i < threads; i++) workers_.emplace_back([this] { worker_main(); });
}

JobSystem::~JobSystem() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
  }
  wake_.notify_all();
  for (auto &t : workers_) t.join();
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
  int allowed = std::min<int>((int)workers_.size(), max_threads_ - 1);
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

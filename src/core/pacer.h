// SPDX-License-Identifier: GPL-2.0-or-later
// Frame pacing (task 0084): wait until a frame's turn under a frame-rate cap without oversleeping. A blocking wait can
// come back late by up to the system timer's tick - 15.6 ms on Windows unless raised - and a frame that leaves less
// than a tick to wait then loses a whole tick more: a 60 fps cap ran at 33 fps (docs/PERFORMANCE.md, pass 10). The
// pacer learns how late its blocking waits run and blocks only while the deadline is further off than that; the rest
// it waits without blocking (polling for input, yielding the thread). The idea is the one games use for a precise
// cap: sleep for most of the gap, then wait the remainder out (GEA Vol. I 8.5, "the game loop").
#pragma once

#include <algorithm>
#include <functional>

namespace bl {

class FramePacer {
 public:
  using Clock = std::function<double()>;           // seconds, monotonic
  using Wait = std::function<void(double seconds)>;  // block up to `seconds` (0: return at once); may end late, or early

  /* Returns once `now()` has reached `deadline` (within a tenth of a millisecond). */
  void wait_until(double deadline, const Clock &now, const Wait &wait) {
    bool blocked = false;
    for (double t = now(); deadline - t > kClose; t = now()) {
      const double left = deadline - t;
      if (left <= late_ + kClose) {
        wait(0.0);  // too close to block: poll and yield
        continue;
      }
      const double ask = left - late_;
      wait(ask);
      learn(now() - (t + ask));
      blocked = true;
    }
    /* A margin as long as the whole wait never blocks, so it would never learn again (one late wake-up - a debugger,
     * a busy machine - would leave the loop polling for good): it shrinks a little each time instead. Under a timer as
     * coarse as 15.6 ms that costs one late frame in about seventy; Blendity's platforms have precise waits. */
    if (!blocked) late_ = std::max(kMinLate, late_ * 0.98);
  }
  /* How late a blocking wait has been running lately, in seconds. */
  double lateness() const { return late_; }

 private:
  static constexpr double kClose = 0.0001;
  /* A late wait raises the margin at once; on-time waits ease it back down slowly, so one lucky wait doesn't
   * bring back the oversleeping. */
  void learn(double late) {
    if (late > late_) late_ = std::min(kMaxLate, late);
    else late_ = std::max(kMinLate, late_ * 0.95 + std::max(0.0, late) * 0.05);
  }
  static constexpr double kMinLate = 0.0005, kMaxLate = 0.02;
  double late_ = 0.002;
};

}  // namespace bl

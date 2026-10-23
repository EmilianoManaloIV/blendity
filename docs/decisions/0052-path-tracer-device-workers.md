# 0052: Persistent device workers and two submissions in flight for the path tracer

- **Status:** accepted (task 0161; the roadmap approved 2026-10-21: the async GPU pipeline before the OptiX device)
- **Date:** 2026-10-22
- **Requirements:** R-03 (rendering), Q-01 (responsiveness)

## Context
`PathTracer::render(budget, max)` spawned a thread per card on every call, each submitting one chunk of samples at
a time and waiting for its fence, then joined them all before returning. The editor called it once a frame with a
40 ms budget for F12 and 12 ms for the Camera Preview, so a card worked for the slice and sat idle for the rest of
the frame (uploads, merges, the UI, the next frame's events), and the main thread blocked for the slice. Task 0159's
clock showed the cards 99 % busy inside each submission and, over a 2048-sample F12 of the default scene, busy for
41-43 % of the render's wall time (a card's average over the two): the idle time was between submissions.

## Decision
- **A worker per card, for the life of its slot.** `GpuSlot` gains a thread that idles on a condition variable
  until a render starts, then claims chunks from one shared counter and keeps two submissions in flight on its
  device: `Renderer::submit()` records into one of two command buffers and submits without waiting,
  `wait_oldest()` takes the oldest fence and its timestamps. The worker downloads the card's sums into a host copy
  when a merge is wanted (its flights drained first) and when it stops.
- **The chunk cap halves.** The guided self-scheduling stays (half the card's fair share of what is left, at most
  64 samples), but a chunk is capped at 50 ms instead of task 0051's 100 ms: with two in flight, a pause - a camera
  move, a stop - waits for at most about 100 ms of work plus the download. The driver's 2 s timeout is further away
  than before.
- **`render()` polls.** With cards at work it starts them toward the target if needed (the counter restarts from
  the cards' finished counts, never from a stale total), renders the CPU's own samples for the budget when the CPU
  is a device, waits what is left of the budget for the cards (a long budget ends with every sample done, as the
  stress rows need), takes the counts and the cards' times, merges the host copies when due, and returns. The
  editor passes a short budget (4 ms for F12, 2 ms for the Camera Preview) so the frame stays quick and the cards
  render between frames, and resolves F12's picture only when samples were merged. The CPU-only path is unchanged.
- **Per-slot state, one lock.** A slot's `active`, `failed`, `quit`, `merge_wanted`, counts, rates and timing sums
  are under `work_mutex_`; its host copy under its own mutex; the shared counter is atomic. A slot counts as active
  from `start_workers`, so a poll never sees the cards idle before their threads wake, and the main thread drives a
  device itself only when the go flag is off and the slot idle. The stats are written by the main thread alone
  (`gather_workers` moves the slots' sums into them).
- **`pause()` before anything touches a device or the sums**: `reset`, `set_settings`, `set_params`,
  `upload_gpus`, `sync_gpus`, `merge_aux` (which merges fresh sums first, so the planes and the count agree), the
  editor's Stop / sequence / animation stops, the destructor. It turns the go flag off, waits until every slot
  idles, and refreshes the counts; cheap when nothing runs. A slot that leaves (dropped, unwanted, a failed upload,
  a tracer rebuilt without cards) has its worker stopped by its own quit flag first; the others sleep on.
- **The picture is the same average.** Samples are whole frames claimed from one counter; the merge adds the
  CPU's sums and each card's downloaded sums and counts the samples those copies hold, so what is shown is always
  a consistent average (`samples()` is what the picture holds, `done_total` what has finished). A reset clears the
  host copies with the sums.
- **A failed card leaves** (its worker records the error and idles; the next `render()` drops the slot under
  `pause()`), as before.
- **A test hook**, `BLENDITY_GPU_FAKE_SLOW=<ms>`, makes every submission take that long on the worker's side,
  so a test can show the main thread unblocked while a slow card renders.

## Consequences
- The editor's F12 frames are short (6-8 ms headless) except the one in each merge interval that resolves the
  1280x720 picture (about 12 ms more); the roadmap's "every frame under 10 ms" is therefore "every frame but the
  merging one", and the test says so. The cards' busy share over a 2048-sample F12 rises; the numbers are in the
  task doc and PERFORMANCE.md Pass 13. The stress gpu rows (back-to-back renders, the card already busy) stay
  within noise.
- One more thread per card, idle between renders. A `PathTracer` is no longer copyable (it never was moved).
- A transparent F12 (its alpha from the coverage plane) or one through camera filters reads the auxiliary planes
  at every merge, and `merge_aux` pauses the cards for it (about 100 ms of work in flight): those renders keep
  the cards idle for a moment each merge interval. Taking the planes from the workers' own downloads is the next
  step if it shows.
- The busy time of two overlapping flights is taken as a union on the card's clock, so the Profiler's share stays
  under 100 % per card.
- `PTStats::gpu_wait_ms` now measures the worker's wait on the oldest fence, which overlaps the other submission
  in flight: it is no longer idle time, and the Profiler's busy share over the render's wall time (per card) is the
  number to read (task 0159).

## References
- ADR 0036 / 0051 (the views' submissions), task 0051 (the TDR cap), task 0159 (the card's clock), Vulkan's
  fences and command buffers in flight (one per submission; a third waits for the oldest); Cycles' session thread
  (`intern/cycles/session/session.cpp`), which renders on its own thread while the window draws the result.

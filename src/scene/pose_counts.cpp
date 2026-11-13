// SPDX-License-Identifier: GPL-2.0-or-later
// PoseCounts (round 468, task 0444): see pose_counts.h. Modelled on MeshCounts (mesh.cpp): a count and microseconds a stat.
#include "pose_counts.h"

#include <atomic>

namespace bl {

namespace {
constexpr int kStats = (int)PoseStat::Count;
enum Extra { kMoves, kNameWalks, kSolverIterations, kSpringChains, kKeysKept, kKeysStale, kBindings, kSolverWrites, kExtras };
std::atomic<uint64_t> g_count[kStats], g_us[kStats], g_extra[kExtras];
/* The calling thread's own share of the same tallies: a test (or a row) that calls the scene layer directly reads these,
 * so a worker of another editor counting at the same moment can't change what it sees. */
struct ThreadTally {
  uint64_t count[kStats] = {}, us[kStats] = {}, extra[kExtras] = {};
};
thread_local ThreadTally tl_tally;

void add_extra(Extra e, uint64_t n) {
  g_extra[e].fetch_add(n, std::memory_order_relaxed);
  tl_tally.extra[e] += n;
}

/* A PoseCounts from one set of tallies (the process's or a thread's). */
template<class Count, class Us, class Ext> PoseCounts counts_from(Count &&n, Us &&us, Ext &&extra) {
  auto ms = [&](PoseStat s) { return (double)us(s) / 1000.0; };
  PoseCounts c;
  c.skin_keys = n(PoseStat::SkinKey), c.skin_deforms = n(PoseStat::SkinDeform);
  c.shape_hashes = n(PoseStat::ShapeHash), c.shape_applies = n(PoseStat::ShapeApply);
  c.evaluations = n(PoseStat::Evaluate), c.outline_shells = n(PoseStat::OutlineShell);
  c.constraint_solves = n(PoseStat::Constraints), c.constraint_moves = extra(kMoves);
  c.name_walks = extra(kNameWalks);
  c.skin_keys_kept = extra(kKeysKept), c.skin_keys_stale = extra(kKeysStale), c.skin_bindings = extra(kBindings);
  c.clip_applies = n(PoseStat::ClipApply);
  c.solver_solves = n(PoseStat::Solver), c.solver_iterations = extra(kSolverIterations);
  c.solver_writes = extra(kSolverWrites);
  c.spring_steps = n(PoseStat::Spring), c.spring_chains = extra(kSpringChains);
  c.ms_skin_keys = ms(PoseStat::SkinKey), c.ms_skin_setup = ms(PoseStat::SkinSetup), c.ms_skin_deforms = ms(PoseStat::SkinDeform);
  c.ms_shape_hashes = ms(PoseStat::ShapeHash), c.ms_shape_applies = ms(PoseStat::ShapeApply);
  c.ms_evaluations = ms(PoseStat::Evaluate), c.ms_outline_shells = ms(PoseStat::OutlineShell);
  c.ms_constraints = ms(PoseStat::Constraints), c.ms_constraints_fk = ms(PoseStat::ConstraintsFK);
  c.ms_constraints_ik = ms(PoseStat::ConstraintsIK), c.ms_constraints_vrc = ms(PoseStat::ConstraintsVRC);
  c.ms_clip_applies = ms(PoseStat::ClipApply), c.ms_solver = ms(PoseStat::Solver), c.ms_springs = ms(PoseStat::Spring);
  return c;
}
}  // namespace

void pose_add(PoseStat s, double ms, uint64_t count) {
  const int i = (int)s;
  const uint64_t us = (uint64_t)(ms * 1000.0);
  if (count) g_count[i].fetch_add(count, std::memory_order_relaxed);
  g_us[i].fetch_add(us, std::memory_order_relaxed);
  tl_tally.count[i] += count, tl_tally.us[i] += us;
}
void pose_add_moves(uint64_t n) { add_extra(kMoves, n); }
void pose_add_name_walks(uint64_t n) { add_extra(kNameWalks, n); }
void pose_add_solver_iterations(uint64_t n) { add_extra(kSolverIterations, n); }
void pose_add_solver_writes(uint64_t n) { add_extra(kSolverWrites, n); }
void pose_add_spring_chains(uint64_t n) { add_extra(kSpringChains, n); }
void pose_add_skin_keys_kept(uint64_t n) { add_extra(kKeysKept, n); }
void pose_add_skin_keys_stale(uint64_t n) { add_extra(kKeysStale, n); }
void pose_add_skin_bindings(uint64_t n) { add_extra(kBindings, n); }

PoseCounts pose_counts() {
  return counts_from([](PoseStat s) { return g_count[(int)s].load(std::memory_order_relaxed); },
                     [](PoseStat s) { return g_us[(int)s].load(std::memory_order_relaxed); },
                     [](Extra e) { return g_extra[e].load(std::memory_order_relaxed); });
}

PoseCounts pose_counts_this_thread() {
  const ThreadTally &t = tl_tally;
  return counts_from([&](PoseStat s) { return t.count[(int)s]; }, [&](PoseStat s) { return t.us[(int)s]; }, [&](Extra e) { return t.extra[e]; });
}

PoseCounts PoseCounts::operator-(const PoseCounts &o) const {
  PoseCounts d;
  d.skin_keys = skin_keys - o.skin_keys, d.skin_deforms = skin_deforms - o.skin_deforms;
  d.shape_hashes = shape_hashes - o.shape_hashes, d.shape_applies = shape_applies - o.shape_applies;
  d.evaluations = evaluations - o.evaluations, d.outline_shells = outline_shells - o.outline_shells;
  d.constraint_solves = constraint_solves - o.constraint_solves, d.constraint_moves = constraint_moves - o.constraint_moves;
  d.name_walks = name_walks - o.name_walks, d.clip_applies = clip_applies - o.clip_applies;
  d.skin_keys_kept = skin_keys_kept - o.skin_keys_kept, d.skin_keys_stale = skin_keys_stale - o.skin_keys_stale;
  d.skin_bindings = skin_bindings - o.skin_bindings;
  d.solver_solves = solver_solves - o.solver_solves, d.solver_iterations = solver_iterations - o.solver_iterations;
  d.solver_writes = solver_writes - o.solver_writes;
  d.spring_steps = spring_steps - o.spring_steps, d.spring_chains = spring_chains - o.spring_chains;
  d.ms_skin_keys = ms_skin_keys - o.ms_skin_keys, d.ms_skin_setup = ms_skin_setup - o.ms_skin_setup;
  d.ms_skin_deforms = ms_skin_deforms - o.ms_skin_deforms;
  d.ms_shape_hashes = ms_shape_hashes - o.ms_shape_hashes, d.ms_shape_applies = ms_shape_applies - o.ms_shape_applies;
  d.ms_evaluations = ms_evaluations - o.ms_evaluations, d.ms_outline_shells = ms_outline_shells - o.ms_outline_shells;
  d.ms_constraints = ms_constraints - o.ms_constraints, d.ms_constraints_fk = ms_constraints_fk - o.ms_constraints_fk;
  d.ms_constraints_ik = ms_constraints_ik - o.ms_constraints_ik, d.ms_constraints_vrc = ms_constraints_vrc - o.ms_constraints_vrc;
  d.ms_clip_applies = ms_clip_applies - o.ms_clip_applies, d.ms_solver = ms_solver - o.ms_solver, d.ms_springs = ms_springs - o.ms_springs;
  return d;
}

bool PoseCounts::any() const {
  return skin_keys || skin_deforms || shape_hashes || shape_applies || constraint_solves || clip_applies || solver_solves || spring_steps;
}

}  // namespace bl

// SPDX-License-Identifier: GPL-2.0-or-later
// What a posing frame costs below the editor (round 468, task 0444): the skin's cache key and its deform, shape keys,
// the evaluations, the outline shells, the constraint solve and its parts, the clip, the AutoPose solver and the
// springs - a count and the milliseconds each took, process-wide as MeshCounts is (the Profiler, the stress suite and
// tests read it). Subtract two readings for what happened in between.
#pragma once

#include "../core/core.h"
#include "../core/profiling.h"

#include <cstdint>

namespace bl {

struct PoseCounts {
  uint64_t skin_keys = 0;        // the skin's cache keys that walked the bones (round 469: a kept key is skin_keys_kept)
  uint64_t skin_keys_kept = 0;   // ...answered from the kept key: nothing under the character moved
  uint64_t skin_keys_stale = 0;  // a kept key that a fresh walk did not confirm (must stay 0: a write past every funnel)
  uint64_t skin_bindings = 0;    // the per-vertex bone tables built (once a mesh, until its weights or the rig change)
  uint64_t skin_deforms = 0;     // SkinModifier::modify calls that reached the vertex loop
  uint64_t shape_hashes = 0;     // shape_key_hash calls
  uint64_t shape_applies = 0;    // apply_shape_keys calls that added at least one key or the open shape
  uint64_t evaluations = 0;      // GameObject::evaluated_mesh calls that went past the key hit
  uint64_t outline_shells = 0;   // Outline::shell builds (not the cache hits)
  uint64_t constraint_solves = 0;  // solve_constraints calls with a live constraint
  uint64_t constraint_moves = 0;   // ...of which reported a move
  uint64_t name_walks = 0;       // lookups by name that had to walk (past the remembered id)
  uint64_t clip_applies = 0;     // apply_clip calls
  uint64_t solver_solves = 0, solver_iterations = 0;  // autopose::solve calls and the iterations they ran
  uint64_t solver_writes = 0;       // (round 473) transforms the solver's callers wrote into the scene after a solve
  uint64_t spring_steps = 0, spring_chains = 0;       // spring::step calls and the chains they stepped
  double ms_skin_keys = 0, ms_skin_setup = 0, ms_skin_deforms = 0;  // (setup: before the vertex loop, inside the deform's whole)
  double ms_shape_hashes = 0, ms_shape_applies = 0, ms_evaluations = 0, ms_outline_shells = 0;
  double ms_constraints = 0, ms_constraints_fk = 0, ms_constraints_ik = 0, ms_constraints_vrc = 0;
  double ms_clip_applies = 0, ms_solver = 0, ms_springs = 0;
  PoseCounts operator-(const PoseCounts &o) const;
  /* Anything at all counted (the Profiler's Posing block shows only then). */
  bool any() const;
};
PoseCounts pose_counts();
/* The calling thread's own share of them: what this thread's calls counted, whatever a worker of another editor counts
 * meanwhile (a unit test that calls the scene layer directly reads these; the editor's frame reads the process's). */
PoseCounts pose_counts_this_thread();

/* Which tally a PoseTimer or pose_add feeds. */
enum class PoseStat {
  SkinKey, SkinSetup, SkinDeform, ShapeHash, ShapeApply, Evaluate, OutlineShell,
  Constraints, ConstraintsFK, ConstraintsIK, ConstraintsVRC, ClipApply, Solver, Spring, Count
};

/* Adds `count` and `ms` to a tally (relaxed atomics: evaluations also run on workers). SkinSetup and the constraint parts
 * take a time only. */
void pose_add(PoseStat s, double ms, uint64_t count = 1);
/* The extra tallies that aren't a time of their own. */
void pose_add_moves(uint64_t n);
void pose_add_name_walks(uint64_t n);
void pose_add_solver_iterations(uint64_t n);
void pose_add_solver_writes(uint64_t n);
void pose_add_spring_chains(uint64_t n);
void pose_add_skin_keys_kept(uint64_t n);
void pose_add_skin_keys_stale(uint64_t n);
void pose_add_skin_bindings(uint64_t n);

/* Wall time on the calling thread to the end of the scope. Put it around calls that run a few hundred times a frame at
 * most - never inside a per-vertex, per-face or per-object loop. `count` false: time only (a part, or a call that
 * turned out to do nothing). */
struct PoseTimer {
  PoseStat stat;
  ScopedTimer t;
  bool count = true;
  explicit PoseTimer(PoseStat s) : stat(s) {}
  ~PoseTimer() { pose_add(stat, t.ms(), count ? 1 : 0); }
};

}  // namespace bl

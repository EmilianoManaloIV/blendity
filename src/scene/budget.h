// SPDX-License-Identifier: GPL-2.0-or-later
// Performance budgets (round 355, task 0332): what an avatar, a world or a game asset costs - triangles, meshes, material
// slots, bones, spring chains, texture memory, lights, shape keys - against a target's limits, VRChat's Performance
// Ranks among them (creators.vrchat.com/avatars/avatar-performance-ranking-system). The counts are the scene's own; the
// editor shows them in the Profiler's Budget section, the console's `budget`, and the export's summary.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bl {

class Scene;
class GameObject;

/* What is counted, each row of a budget. */
enum class BudgetRow { Triangles = 0, SkinnedMeshes, Meshes, MaterialSlots, Bones, SpringChains, SpringBones, TextureMB, Lights, Count };
const char *budget_row_name(BudgetRow r);

/* One rank of a target: its name and each row's limit (-1: no limit on that row). */
struct BudgetRank {
  const char *name;
  double limit[(int)BudgetRow::Count];
};

/* A target: its ranks, best first ("Excellent", "Good", "Medium", "Poor"); over the last is the target's "Very Poor". */
struct BudgetTarget {
  const char *name;
  std::vector<BudgetRank> ranks;
  const char *over = "Very Poor";
};
const std::vector<BudgetTarget> &budget_targets();  // VRChat PC, VRChat Quest (Android)
int budget_target_index(const std::string &name);   // by name, case blind; -1 when none

/* The counts of what `objects` hold (each once; their meshes as the stack makes them). */
struct BudgetCounts {
  double value[(int)BudgetRow::Count] = {};
  size_t shape_keys = 0;  // (shown, not ranked: VRChat has no limit on them)
};
BudgetCounts budget_count(const std::vector<GameObject *> &objects);
/* Review 355: the counts of what an export writes - its items' meshes (an Outline's shell is an item of its own), their
 * slots and textures, and the joints of each skeleton once - not lights or what the file leaves out. */
struct ExportItem;
BudgetCounts budget_count_items(const std::vector<ExportItem> &items);

/* The best rank every row fits (its index), or ranks.size() when even the last is exceeded; and per row the best rank
 * it alone fits. */
struct BudgetResult {
  int rank = 0;
  int row_rank[(int)BudgetRow::Count] = {};
};
BudgetResult budget_rank(const BudgetTarget &t, const BudgetCounts &c);
std::string budget_rank_name(const BudgetTarget &t, int rank);

}  // namespace bl

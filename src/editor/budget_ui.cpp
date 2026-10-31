// SPDX-License-Identifier: GPL-2.0-or-later
// The performance budget (round 355, task 0332): the Profiler's Budget section, the console's `budget`, the export's
// line and Decimate to fit - the counts and the ranks are scene/budget's.
#include "editor.h"

#include "../scene/budget.h"
#include "../core/core.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bl {

namespace {
const BudgetTarget &target_of(int i) {
  const std::vector<BudgetTarget> &t = budget_targets();
  return t[(size_t)std::max(0, std::min(i, (int)t.size() - 1))];
}

std::string value_text(BudgetRow r, double v) {
  return r == BudgetRow::TextureMB ? strprintf("%.1f", v) : strprintf("%.0f", v);
}

/* A rank's colour: the best two green, Medium and Poor amber, over the target red. */
uint32_t rank_color(const ui::Theme &th, int rank, int ranks) {
  if (rank <= 1) return th.ok;
  if (rank == 2) return th.warning;
  return rank < ranks ? th.warning : th.error;
}
}  // namespace

/* What a budget counts: the selected objects and everything under them, else the whole scene. */
std::vector<GameObject *> Editor::budget_objects() {
  ExportOptions o;
  o.selection_only = !selected_objects(true).empty();
  return export_candidates(o);
}

/* The Profiler's Budget section: the target, its rank for what's selected (or the scene), and a row a count - its value,
 * the limit of the rank above, coloured by the rank it alone reaches; Decimate to fit when the triangles hold it back. */
void Editor::draw_budget(ui::Layout &lay) {
  auto &u = ui_;
  const std::vector<BudgetTarget> &targets = budget_targets();
  std::vector<const char *> names{"None"};
  for (const BudgetTarget &t : targets) names.push_back(t.name);
  Recti head = lay.row(u.row_h() + u.px(2));
  u.label({head.x, head.y, head.w / 3, head.h}, "Budget", u.theme.text_bright);
  int pick = budget_target_ + 1;
  if (u.combo(u.id("budget_target"), {head.x + head.w / 3, head.y, head.w / 3, head.h}, pick, names.data(), (int)names.size())) budget_target_ = pick - 1;
  u.tooltip("The target to rank against - VRChat's Avatar Performance Ranks (creators.vrchat.com): Excellent, Good,\n"
            "Medium, Poor, and Very Poor past Poor (Quest hides Very Poor avatars by default). None: no ranking, and\n"
            "exports don't report one.");
  if (budget_target_ < 0) return;
  /* The counts kept half a second (review 355: every frame walked every mesh and looked up every texture). */
  const double now = clock_now();
  if (now - budget_cache_.at > 0.5) budget_cache_ = {budget_count(budget_objects()), now};
  const BudgetCounts &c = budget_cache_.counts;
  const BudgetTarget &t = target_of(budget_target_);
  const BudgetResult r = budget_rank(t, c);
  const int ranks = (int)t.ranks.size();
  const std::string rank = budget_rank_name(t, r.rank);
  const std::string line = strprintf("%s - %s", rank.c_str(), selected_objects(true).empty() ? "the scene" : "the selection");
  u.label({head.x + head.w * 2 / 3 + u.px(6), head.y, head.w / 3 - u.px(6), head.h}, line, rank_color(u.theme, r.rank, ranks));
  u.tooltip(line);
  if (u.record_buttons) u.button_rects["shown:budget " + line] = head;
  for (int row = 0; row < (int)BudgetRow::Count; row++) {
    const int at = r.row_rank[row];
    const std::string limit = at > 0 && at <= ranks ? value_text((BudgetRow)row, t.ranks[(size_t)at - 1].limit[row]) : std::string("-");
    Recti rr = lay.row();
    u.label({rr.x, rr.y, rr.w / 2, rr.h}, budget_row_name((BudgetRow)row), u.theme.text_dim);
    u.tooltip(budget_row_name((BudgetRow)row));
    const std::string v = value_text((BudgetRow)row, c.value[row]) + "  (" + budget_rank_name(t, at) + (at > 0 ? ", " + budget_rank_name(t, at - 1) + " at " + limit : std::string()) + ")";
    u.label({rr.x + rr.w / 2, rr.y, rr.w / 2, rr.h}, v, rank_color(u.theme, at, ranks));
    if (u.record_buttons) u.button_rects["shown:budget " + std::string(budget_row_name((BudgetRow)row)) + " " + v] = rr;
  }
  Recti sk = lay.row();
  u.label({sk.x, sk.y, sk.w / 2, sk.h}, "Shape Keys", u.theme.text_dim);
  u.label({sk.x + sk.w / 2, sk.y, sk.w / 2, sk.h}, strprintf("%zu (not ranked)", c.shape_keys));
  if (r.row_rank[(int)BudgetRow::Triangles] <= 1) return;
  Recti fix = lay.row(u.row_h() + u.px(2));
  if (u.button_enabled({fix.x, fix.y, fix.w, fix.h}, "Decimate to Good", meshops::decimate_available(), "This build has no meshoptimizer (Blender's libraries)")) {
    budget_fix_triangles(1);
    budget_cache_.at = 0.0;
  }
  u.tooltip("A Decimate modifier on each mesh of 500 triangles or more (before its Skin; its weights and shape keys kept),\n"
            "one ratio for all, so the triangles fit the Good rank. Change its ratio or remove it; Undo takes it back.");
}

/* Decimate to fit: each mesh's own Decimate modifier (added before its Skin, so the skin deforms the result and a pose
 * doesn't decimate again) cut by one ratio, so the triangles meet `rank`'s limit - the meshes under 500 triangles (a
 * ground plane, a prop) left whole, the cut spread over the rest; an existing Decimate's ratio multiplied, as the counts
 * are its result. */
bool Editor::budget_fix_triangles(int rank) {
  if (budget_target_ < 0) return Log::warn("Decimate to fit: choose a budget target first (budget pc|quest, or the Profiler's Budget)"), false;
  return budget_fix_triangles(rank, budget_objects(), budget_target_);
}

bool Editor::budget_fix_triangles(int rank, const std::vector<GameObject *> &objs, int target) {
  const BudgetTarget &t = target_of(target);
  if (rank < 0 || rank >= (int)t.ranks.size()) return Log::warn("Decimate to fit: no such rank"), false;
  const double limit = t.ranks[(size_t)rank].limit[(int)BudgetRow::Triangles];
  return decimate_to(objs, {}, limit, strprintf("%s's %s", t.name, t.ranks[(size_t)rank].name));
}

/* The cut itself (round 359: CATS' decimation modes share it): `cut_from`'s meshes of 500 triangles or more cut by one
 * ratio so all of them and `fixed` (counted, never cut) come to `limit`; an existing Decimate's ratio multiplied.
 * `drop_keys` (CATS' Full): the cut meshes' shape keys go first - only theirs, and only once a cut is certain (review
 * 359: a refused cut had dropped every key without a word). */
bool Editor::decimate_to(const std::vector<GameObject *> &cut_from, const std::vector<GameObject *> &fixed, double limit, const std::string &what, bool drop_keys) {
  if (!meshops::decimate_available()) return Log::warn("Decimate to fit: this build has no meshoptimizer (Blender's libraries)"), false;
  constexpr double kFloor = 500.0;  // (a mesh this small isn't what holds the rank back)
  double small = budget_count(fixed).value[(int)BudgetRow::Triangles], big = 0.0;
  std::vector<GameObject *> cut;
  for (GameObject *g : cut_from) {
    if (std::find(fixed.begin(), fixed.end(), g) != fixed.end()) continue;
    const double n = budget_count({g}).value[(int)BudgetRow::Triangles];
    if (n <= 0.0) continue;
    if (n < kFloor) small += n;
    else big += n, cut.push_back(g);
  }
  if (small + big <= limit) return Log::info("Decimate to fit: the triangles fit already (%.0f of %.0f)", small + big, limit), false;
  if (big <= 0.0 || limit <= small) return Log::warn("Decimate to fit: the meshes it may not cut pass %s (%.0f of %.0f) - nothing to cut", what.c_str(), small, limit), false;
  const float ratio = (float)std::max(0.01, (limit - small) / big * 0.98);  // (a little under: a decimator lands near, not on, its ratio)
  size_t dropped = 0;
  for (GameObject *g : drop_keys ? cut : std::vector<GameObject *>()) {
    Mesh &m = *mesh_make_mutable(g->get<MeshFilter>()->mesh);
    dropped += m.shape_keys.size();
    m.shape_keys.clear();
    m.touch();
  }
  if (dropped) Log::info("Decimate to fit: %zu shape key(s) dropped from the meshes cut (Full)", dropped);
  for (GameObject *g : cut) {
    DecimateModifier *d = g->get<DecimateModifier>();
    if (d) {
      d->ratio = std::max(0.01f, d->ratio * ratio);
      continue;
    }
    d = g->add<DecimateModifier>();
    d->ratio = ratio;
    const auto skin = std::find_if(g->components.begin(), g->components.end(), [](const std::unique_ptr<Component> &c) { return std::strcmp(c->type_name(), SkinModifier::kName) == 0; });
    if (skin != g->components.end()) std::rotate(skin, g->components.end() - 1, g->components.end());  // (before the Skin)
  }
  mark_changed("Decimate to Fit");
  Log::info("Decimate to fit: %zu mesh(es) at %.0f%% of their triangles, for %s (%.0f)", cut.size(), ratio * 100.0f, what.c_str(), limit);
  return !cut.empty();
}

/* One line: the target, the rank and the counts that hold it back - a warning past the target's last rank. Nothing
 * without a target (the budget is VRChat's: an STL for a printer isn't ranked). */
void Editor::budget_report(const BudgetCounts &c, const char *what) {
  if (budget_target_ < 0) return;
  const BudgetTarget &t = target_of(budget_target_);
  const BudgetResult r = budget_rank(t, c);
  std::string held;
  for (int row = 0; row < (int)BudgetRow::Count; row++)
    if (r.row_rank[row] == r.rank && r.rank > 0) held += (held.empty() ? "" : ", ") + std::string(budget_row_name((BudgetRow)row)) + " " + value_text((BudgetRow)row, c.value[row]);
  const std::string rank = budget_rank_name(t, r.rank);
  const std::string msg = strprintf("Budget (%s): %s %s%s", t.name, what, rank.c_str(), held.empty() ? "" : (" - held by " + held).c_str());
  if (r.rank >= (int)t.ranks.size()) Log::warn("%s", msg.c_str());
  else Log::info("%s", msg.c_str());
}

/* budget [pc|quest|<target name>] | budget fix [good|medium|...]: the rows in the Console; Decimate to fit. */
void Editor::budget_command(const std::vector<std::string> &t) {
  const std::string a = t.size() > 1 ? to_lower(t[1]) : "";
  if (a == "fix") {
    if (budget_target_ < 0) budget_target_ = 0;  // (no target yet: VRChat PC, as `budget` alone would say)
    const BudgetTarget &bt = target_of(budget_target_);
    int rank = 1;
    for (size_t k = 0; k < bt.ranks.size(); k++)
      if (t.size() > 2 && to_lower(bt.ranks[k].name) == to_lower(t[2])) rank = (int)k;
    if (t.size() > 2 && to_lower(bt.ranks[(size_t)rank].name) != to_lower(t[2])) return Log::warn("budget fix: excellent, good, medium or poor, not '%s'", t[2].c_str());
    budget_fix_triangles(rank);
    return;
  }
  if (budget_target_ < 0) budget_target_ = 0;  // (asked for: VRChat PC unless a target is named)
  if (a == "pc" || a == "quest") budget_target_ = std::max(0, budget_target_index(a == "pc" ? "VRChat PC" : "VRChat Quest"));
  if (!a.empty() && a != "pc" && a != "quest") {
    std::string name;
    for (size_t k = 1; k < t.size(); k++) name += (name.empty() ? "" : " ") + t[k];
    const int i = budget_target_index(name);
    if (i < 0) return Log::warn("budget: pc, quest or a target's name (VRChat PC, VRChat Quest) | budget fix [good|medium|poor]");
    budget_target_ = i;
  }
  const BudgetTarget &bt = target_of(budget_target_);
  const std::vector<GameObject *> objs = budget_objects();
  const BudgetCounts c = budget_count(objs);
  const BudgetResult r = budget_rank(bt, c);
  for (int row = 0; row < (int)BudgetRow::Count; row++)
    Log::info("  %-36s %10s  %s", budget_row_name((BudgetRow)row), value_text((BudgetRow)row, c.value[row]).c_str(), budget_rank_name(bt, r.row_rank[row]).c_str());
  budget_report(c, selected_objects(true).empty() ? "the scene" : "the selection");
}

}  // namespace bl

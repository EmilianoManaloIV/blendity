## 2026-11-26 (round 641): Linux CI installs missing packages from Ubuntu

**Asked:** Repair the failing Linux check on PR #504.

**Changed (task 0623)**
- The two Linux build jobs and three test jobs check their installed packages first. When a dependency is missing, they use the signed Ubuntu archive and verify the resulting package state.

**Checked:** Bash syntax, package setup simulations and the five workflow package lists.

## 2026-11-19 (round 595): Instance uploads during a drag

**Measured (task 0577).** Writing only changed Vulkan instance rows reduced writes from 150,000 to one per frame while dragging one of 50,000 cubes. The Shaded drag median fell from 221.7 to 214.5 ms (3.25%), below the required 5% improvement. The candidate was withdrawn. Task 0577 records the matched controls, frame tails and GPU parity checks.

<!-- docs/PERFORMANCE.md -->
append:
## Round 595: moving one cube among 50,000

In the 60-frame Shaded `scene_views` drag, checking which Vulkan instance rows had changed reduced rows written from 150,000 to one per frame. Mean card-write time fell from 8.40 to 0.66 ms, while collection still averaged 87.86 ms. The median frame moved from 221.7 to 214.5 ms (3.25%), below the 5% adoption threshold. The 10,000-cube drag medians were 41.4 and 40.4 ms. The 50,000-cube orbit medians were 14.0 and 12.3 ms with zero rows written in both builds. The candidate was withdrawn. [Task 0577](tasks/0577-moving-object.md) records the matched conditions and tails.

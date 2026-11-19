## 2026-11-19 (round 594): Faster first open of the Textures window

**Asked:** Bring the first open with 2,000 procedural textures within the 100 ms frame budget.

**Changed (task 0576)**

- Walk the Assets tree without collecting file sizes or modification times. Resolve each directory once for ordinary texture files, and use the existing path conversion for links and other special entries.
- Keep the sorted paths and the texture library's existing loading, filtering and selection behavior.

**Checked:** The same 90 Windows behavior checks passed before and after. Windows release assets/editor groups passed with libraries (3,118/3,872 checks) and dependency-free (3,075/3,846). Linux passed 3,085 assets and 3,846 editor checks, including file links, directory links and an unreadable file. The same focused Linux checks passed on the baseline. All six full benchmark runs completed.

<!-- docs/USABILITY.md -->
after: | Textures could not be deleted from the Textures window.
| Opening the Textures window could pause in projects with thousands of procedural textures. | The browser reuses the resolved directory path for ordinary texture files (round 594). |

<!-- docs/PERFORMANCE.md -->
append:
## Round 594: Textures window first open

The full `asset_windows` section creates the textures with `newtex` before it opens the window. The Textures window now resolves ordinary files' directories once per scan and uses the existing path conversion for special entries. The 2,000-texture first-open survey baseline was 244.7, 255.5 and 268.9 ms against a 100 ms budget. Three quiet before/after pairs measured 281.0 → 77.7, 291.7 → 79.7 and 294.7 → 91.2 ms. The next frame took 2.3–2.9 ms after the change, and the 2,000-texture scroll and filter p95 stayed within 3.1 ms. All Materials, History and command palette controls stayed within budget. Task 0576 records every scale and repeat.

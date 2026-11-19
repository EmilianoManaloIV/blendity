# 0580: Correct the released manual documentation

- **Status:** verified documentation candidate (round 598); pass reconciliation pending
- **Authorization:** owner-assigned documentation closeout, including the hub clarification on standing workflow and owner review.
- **Decisions:** ADR 0104 (books), ADR 0123 (documentation fragments)

## Goal and scope
Address the five findings in the 1.0.0 documentation audit. Keep the 0.13.0 snapshots and historical attribution. Record the pass outcomes once the hub supplies the final stack.

## Acceptance
1. The complete Blender Principled shader path fits the module-map column in the compiled Architecture book. Check the rendered page and the extracted coordinates.
2. Both current-book links in the README resolve to the corresponding release assets. Historical tables keep their snapshot labels.
3. The handoff introduction agrees with the Windows toolset requested by the build and release workflows. Distinguish the older toolset trial from production.
4. The architecture guide accurately describes the committed-key check and the release bibliography and index regeneration.
5. The User Guide overflow control has a descriptive name that renders legibly and extracts without U+001D.
6. The standing rules, handoff and task template record assignment-based authorization, independent verification and the evidence needed for closeout. Child chats route material scope changes and owner-reserved decisions to the hub.
7. Every Claude and Codex role, including leads and the hub, cites the references actually used for consequential handoff claims. Cite the repository path and symbol or revision, the upstream primary source URL and relevant version, or the test fixture, command, run or scratch evidence for an observation. Mark unverified assumptions. Reviewers check that references support the claims, and the hub retains them in the spec, ADR or PR as appropriate.
8. Explicit project-wide workflow changes from the owner in the hub apply to active and future tasks and sessions until superseded, subject to the owner's stated scope. The hub records the rule, reconciles conflicting guidance and updates affected briefs and tooling. One-time permissions remain scoped to their task. Verify the standing-rule link and compare the hourly check and board section with the round base.

9. On the combined tree, the startup instructions and standing workflow link to the single “Parallel verification and evidence” procedure from task 0589. Preserve its dependency routing, early smoke test and revision-specific verification guidance together with the project citation rule and task-scoped permissions.

## Verification and delivery
Compile both books with the project LuaLaTeX and Biber workflow in scratch space. Inspect the affected pages, verify release links and run the fragment and citation checks. Obtain independent testing and review. Coordinate the final documentation fold with the hub after the stack is ready.

## Audit results on the round candidate
- Both latest-release download URLs returned HTTP 200 for the 1.0.0 attachments. The tracked 0.13.0 books and historical tables retain their original attribution and labels.
- Both Windows workflows and the handoff introduction specify MSVC 14.51. The branch-only 14.44 run compiled and passed quick stress after splitting the strings. Four Catmull-Clark checks failed.
- The committed citation check passed with 182 of 182 keys cited. The release regeneration path produced 184 of 184 on the round base. Both manuals compiled with the project LuaLaTeX and Biber workflow.
- Poppler rendered the corrected pages. On Architecture physical page 14, the three pieces of the shader path start at x = 406.56 pt and end by x = 500.02 pt. On User Guide physical page 11, “double-chevron overflow button” renders and extracts in full.
- The compiled copies demonstrate the source fixes. The public 1.0.0 attachments still contain the audited defects.

## Joined documentation verification
The closeout was applied to the render-fix stack at `3e5f5a65` ([PR 490](https://github.com/EmilianoManaloIV/blendity/pull/490)), which contains the workflow grant rules at `47602749` ([PR 489](https://github.com/EmilianoManaloIV/blendity/pull/489)). The startup and standing instructions resolve to the single verification procedure in [METHOD.md](../handoff/METHOD.md#parallel-verification-and-evidence). After release notification, the next worker acquires the machine lock or waits for the hub to grant the GPU before starting.

All 60 book and image inputs on this candidate matched the compiled inputs, including a fresh generation of the bibliography and three indices with `tools/make_books.py`. Text comparisons normalized line endings. The 69-page Architecture and 76-page User Guide PDFs and their Poppler page renders are recorded under the round's scratch directory `build/docs598/` in the original worktree. The committed-key and fragment checks passed on this stack. The verified candidate contains 15 fragments. At preparation time, `origin/main` at `4f880bb3` contained zero numbered fragments, so the shared-document fold stays separate until eligible merges reach main.

## Pass outcomes on the Workshop stack

Round 598 merged Workshop publication head `899fca32` in `f5dfb4ef`. The original audit and render-fix verification above describe their respective revisions.

| Work | Verified outcome and limits |
| --- | --- |
| Handoff follow-ups | Items 9 (stable command inventory) and 14 (face-corner paint) met their acceptance checks. Item 19 remains open: MSVC 14.52 failed the full suite in all three tested configurations. [PR 477](https://github.com/EmilianoManaloIV/blendity/pull/477), [PR 480](https://github.com/EmilianoManaloIV/blendity/pull/480), [PR 485](https://github.com/EmilianoManaloIV/blendity/pull/485) |
| Lighting status, 0574 | Narrow panes show the update mode and hover reveals the complete status. The twelve Windows HCI configurations matched baseline. The hosted Linux suite passed 125,311 checks. [Task 0574](0574-lighting-status-labels.md) |
| Textures first open, 0576 | In three paired Windows runs with 2,000 already-loaded procedural textures, first open went from 281.0, 291.7 and 294.7 ms to 77.7, 79.7 and 91.2 ms. All three met the 100 ms budget. [Measurement conditions and controls](0576-textures-window.md) |
| Moving-object experiment, 0577 | The 50,000-cube drag median went from 221.7 to 214.5 ms. The 3.25% gain missed the 5% adoption threshold. The candidate was withdrawn. [Task 0577](0577-moving-object.md) |
| Workshop, 0582–0588 | Fresh Windows and hosted Linux builds each passed 6,160 focused checks, including detached-source and repeated-import checks. The three real character-and-map scenes retained their renders on Windows after detachment. The verification record describes the missing Alyx base-game textures and other asset limits. [Task 0588](0588-workshop-verification.md), [Linux run 44608047654](https://github.com/EmilianoManaloIV/blendity/actions/runs/44608047654) |
| Pi 5, 0578 | Windows journey timings and hosted ARM64 results have separate records. Physical Pi 5 responsiveness and memory headroom remain unmeasured. [Task 0578](0578-pi5-performance.md) |

The Edit Mode first-frame outcome (0571) awaits its quick-stress receipt and reviewed report. [Run 44609521187](https://github.com/EmilianoManaloIV/blendity/actions/runs/44609521187)

### Final manual build

The normal merge of [Workshop PR 491](https://github.com/EmilianoManaloIV/blendity/pull/491) at `1e7bdb28` adds five producer status records to the first Workshop merge. Both revisions have identical book inputs. Release regeneration initially failed on the Workshop lesson's Steamworks reference. The manual parser in `tools/make_books.py` now records that source with Valve attribution and its supplied URL.

The project commands `--bib --check-cited` and `--pdf` regenerated and checked 185/185 references and compiled both books with LuaLaTeX and Biber. The committed snapshot passed `--check-cited` at 182/182. Poppler verified the complete shader path on Architecture page 14, the searchable overflow label on User Guide page 11 and the Workshop instructions on pages 28 and 42 respectively. The lesson source appears on User Guide page 66. The release assets and tracked historical outputs follow the snapshot policy in ADR 0104.

| Compiled manual | Physical pages | SHA-256 |
| --- | ---: | --- |
| Architecture | 69 | `fa1a1037791094fcff927108991377a6b479fcde511abed5aefceebbbceadda8` |
| User Guide | 77 | `231414ec5128bcb949d55c681f48c732d0ca6823ce45ed53acd2195b4afb1b0e` |

The build log, PDFs, page renders and extraction report are in the original round worktree under `build/docs598/joined-workshop/`. All 25 fragments passed the applicability check. Main at `4f880bb3` contains zero numbered fragments eligible for folding.

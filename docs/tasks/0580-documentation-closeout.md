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

## Evidence still needed for pass closeout
- Record the joined outcome for Edit Mode first-frame work (0571), Lighting status verification (0574) and Textures performance (0576) from their final revisions and run receipts. For the moving-object experiment (0577), retain the withdrawal and measured 3.25% median improvement against its 5% target when its reviewed report joins the stack.
- Keep Windows journey timings and hosted ARM64 results distinct from physical Pi 5 measurements. The physical board remains unmeasured. [Pi 5 verification](https://github.com/EmilianoManaloIV/blendity/pull/487)
- Record the Workshop import verification on the final joined revision (0582–0588), including fresh app/test objects, the six relevant test groups and the detached-process smoke result. Use the hub's reviewed outcomes and final CI head to reconcile the affected docs and recheck fragment eligibility.

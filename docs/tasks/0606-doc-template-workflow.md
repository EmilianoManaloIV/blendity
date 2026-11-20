# 0606: Apply the repository documentation templates across subsystems

- **Status:** in review
- **Authorization:** The owner requested in the Hub that all subsystems and the Hub follow the provided templates.
- **Requirements:** C-05, [project documentation duties](../../AGENTS.md#documentation-duties-every-change) and [task spec rules](README.md#rules).
- **Decisions:** [ADR 0123](../decisions/0123-changelog-fragments.md) applies. Existing formats cover this workflow clarification.
- **Implementation:** Planning task for shared workflow instructions. The Hub coordinates the active authors and publication.
- **PR:** Pending Hub publication of the cumulative performance pass.

## Goal

A contributor working on any subsystem can find the applicable template and verify the completed document before handoff or publication.

## Scope

- **In** — Apply the repository's task, ADR, changelog fragment and PR formats to the Hub and every subsystem and role. Include applicable pass, handoff and subsystem formats.
- **Out** — Changes to template structure, accepted ADRs, released historical records, publication tooling and CI gates. The task template verification example now follows ADR 0134.

## Likely files

- `AGENTS.md` and `CLAUDE.md` for the shared rule and publication examples.
- `docs/tasks/README.md` for lifecycle states and Codex and Claude role references, and `docs/tasks/TEMPLATE.md` for the local and CI scope example.
- `docs/handoff/METHOD.md` and `docs/handoff/CODEX.md` for the audit and role briefs.
- `changelog.d/624-document-templates.md` and this spec. The Hub and subsystem authors own their other active specs and fragments.

## Acceptance criteria

1. Document the authoritative source for each format. Require real scope, evidence, outcomes and explicit reasons for genuinely inapplicable fields.
2. Make both author and reviewer responsibilities visible in the workflow, including the Hub's own documents and PR body.
3. Check active tasks 0600 through 0606 and related fragments against the provided templates. Check PR content when published. Verify older records and unchanged formats retain their historical meaning.

## Tests and checks to run

- Check template links, task metadata and section order, applicable ADRs and related fragments. Inspect the committed gate range with `git log --no-merges --oneline origin/main..HEAD` and run `bash tools/dev/docs_check.sh origin/main HEAD`.
- Check the cumulative PR body against `.github/pull_request_template.md` at publication. Build, unit and stress checks are inapplicable because this round changes workflow prose only.

## Risks and edge cases

- Compare claims with evidence identified for the candidate. A document can have the template's headings with missing or inaccurate contents.
- Preserve the evidence in active specs when it moves under Outcome. Review the assembled stack and PR body again at publication.

## Documentation to update

- Update `AGENTS.md`, `CLAUDE.md`, `docs/tasks/README.md`, `docs/handoff/METHOD.md`, `docs/handoff/CODEX.md` and the round 624 fragment.
- The editor and shared-doc content are outside the scope of this workflow change, so the fragment needs only its changelog entry.

## Outcome

The [template audit](../handoff/METHOD.md#documentation-template-audit) identifies the formats, the checks
each role performs and the Hub's publication checks. The publication examples use a prepared body file,
and the Codex brief links the audit and asks for the supporting documents in review. The task guide points
Codex and Claude workers to their existing roles and keeps the owner's merge as the transition to `done`.

The working-tree check resolved 22 added or updated links. `bash tools/dev/docs_check.sh origin/main HEAD`
passed on base `bf7e9c3f` and head `56614b44`, with 26 non-merge commits, one authorized fold and ten applicable
fragments. The working tree includes the round 624 fragment. `py tools/dev/changelog.py --self-test` passed
93 checks. `git diff --check` passed. Review found that the task template suggested a full local suite for every task. Its example now matches ADR 0134. The ADR, PR and fragment templates and accepted decisions are unchanged.

The worktree audit confirmed the numbered titles and fragment headings for 0600 through 0606. It
checked the Hub's 0600 and 0601 in `C:/blwt-pass32` and the other tasks in their round worktrees.
All six metadata fields and the section order matched. The 0602 worktree's status needed to be
normalized to `in review`. The Hub owns the joined check and routes each change to its document owner.

For ADR applicability, 0600 plans measurement and verification under ADR 0105 and ADR 0123. Of the
inspected candidates, 0601 adjusts a buffer lifetime behind the same importer
contract, 0602 preserves the first-fit blocks and stable triangle bases in
[ADR 0109](../decisions/0109-the-cpus-share-of-a-card-frame.md), 0603 removes implementation work for
one bevel segment, 0604 preserves draw-list invalidation under
[ADR 0115](../decisions/0115-the-scenes-change-counter.md), and 0605 avoids redundant Windows file
attribute queries within the existing discovery contract. The reviewed scopes fit their existing decisions.
0606 applies the established documentation conventions.

`tools/dev/integrate.sh` generates and replaces its integration PR body. Task 0607 is the
separate round to align that generator with the PR template.

Independent review approved these workflow documents for integration. The joined revision and its published PR still require verification.

### Claude-to-Codex handoff cross-check

The Hub compared the release handoff at v1.0.0 (4f880bb3), main at bf7e9c3f, the two open PRs, and the five implementation worktrees in the engine performance pass. The findings below distinguish repository policy, actual published evidence, and hosting enforcement.

| Area | Verified difference | Disposition |
|---|---|---|
| Task authoring | Hub and subsystem authors initially used abbreviated specs in 0600–0605. The release project already supplied the numbered task template. | Corrected active documents and checked their six metadata fields and eight ordered sections. Authors and Hub reviewers now compare content to the same template. |
| PR publication | PRs 498 and 499 had detailed evidence under custom headings. The integration helper could also replace an existing body during a no-change refresh. | The Hub restored all five PR-template sections on both published PRs and verified their unchanged head SHAs. Task 0607 owns the helper and repeat-refresh regression. |
| Local chain | The migration removed machine-specific default paths. Several documented invocations omit the now-required inputs. Historical shell error handling can also discard a failed group or WSL exit status. | Task 0608 owns a real entry-point regression and the command examples. A chain completion banner by itself is insufficient evidence of a passing suite. |
| Onboarding | In the original development checkout, the selected Blendity folder did not contain AGENTS.md or .codex/agents, although the published tree did. | The startup instructions now require checking those files before dispatch. The Hub used the published-stack worktree and left the owner's original checkout intact. |
| Dispatch and gates | The original Claude role contracts name Sonnet, Sonnet and Opus. The approved Codex equivalents are Sol medium, Sol high and Astra high; the human requested independent roles in sequence and parallelism across file-disjoint tasks. | Preserve the task, independent test and review responsibilities, and point each client at its own model configuration. Worker identity and independence must be evidenced in handoffs, even when CI passes. |
| Owner protections | Protected main has four required build contexts. A green check does not identify the reviewer or authorize merge, force push, deletion or a release tag. The earlier prose allowing requested agent merges conflicted with the Claude deny rule. | Preserve the owner's current merge and tag rules. Verified merged-task cleanup is the owner's narrow Hub-only exception. CI-only compiler install approvals do not grant local installation. |

Sources include the v1.0.0 and current CLAUDE.md and AGENTS.md, .claude/agents and .claude/settings.json, .codex/agents and .codex/rules/blendity.rules, docs/handoff/METHOD.md, tools/dev/chain.sh and integrate.sh, and the verified GitHub bodies for PRs 498 and 499. The review does not treat a configured rule as proof that every historical or client-side action followed it.

The task template now states the same local-versus-CI verification scope as ADR 0134. The task guide reflects the approved cumulative publication procedure. AGENTS.md, CLAUDE.md and METHOD.md show explicit scratch paths. BL_MAIN and BL_WSL_TREE are mandatory. BL_W overrides the Windows default. The Hub reviewed these differences against the project source and tasks 0607–0609.

The Hub saved the prior PR descriptions at C:/blab/pass32/pr-498-prior.md and pr-499-prior.md. Compare the corrected descriptions on [PR 498](https://github.com/EmilianoManaloIV/blendity/pull/498) and [PR 499](https://github.com/EmilianoManaloIV/blendity/pull/499). Tasks 0607 and 0608 track the integration-body and chain repairs.

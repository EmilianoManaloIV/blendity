# 0570 — Carry Blendity's complete project workflow into Codex (round 588)

- **Status** — done
- **Requirements** — Owner's handoff, existing project engineering standards and CLAUDE.md
- **Decisions** — Existing Git and role boundaries, ADR 0123 and ADR 0133
- **PR** — [#481](https://github.com/EmilianoManaloIV/blendity/pull/481), recorded merge `62854e5b`. Targeted `main` and carried #480, #479, #478 and #477.

## Goal

Make every project-owned Claude workflow element discoverable and usable in a clean Codex checkout. Include an auditable mapping from each original element to its replacement, its verification and any execution-surface limitation.

## Inventory and changes

- Compare all tracked CLAUDE.md sections, the three .claude/agents contracts, every entry in .claude/settings.json, the handoff, original workflow template and references in tools, CI, templates, task instructions and AI disclosure. Inventory project commands, skills, hooks and MCP definitions if present. Separate historical records and local-only state from active guidance. Keep historical attribution intact.
- Starting from AGENTS.md, link to the Codex agents, handoff procedure and applicable protections. Correct active task templates, AI attribution and documentation that still direct new Codex work exclusively to Claude or its models. Carry through the original main-session rule for difficult architecture or geometry, independent roles for routine work, and hub ownership of coordination, without changing the owner's current gates.
- Map all ten original project deny entries to project-local Codex command rules where supported. Include safe, offline tests for the prohibited forms and representative permitted fetch, review and task-branch publication forms. Explicitly document what instruction-level rules and GitHub branch protection supply beyond shell pattern matching. Resolve older permission wording in favor of the owner's current directions, including owner-only merges, all four private folders and release-tag consent. Direct human Git use remains the owner's choice.
- Reconcile the machine-specific outer-checkout settings, worktrees, local locks and historical session notes by purpose. Keep credentials, broad machine-specific grants, private chat contents, private project folders and transient locks out of Git. Show which existing helpers and handoff pages carry their workflow responsibilities. Any migration needing new access or a persistent user-wide change goes to the owner first.
- Teach the CI path classifier to recognize approved Codex agent/rule-only changes alongside the existing Claude metadata exemption. Retain required check names and native builds for changes to product or build inputs. Validate the Codex files on the appropriate lightweight check rather than silently accepting malformed configuration.
- Document cold-start behavior for the developer's original subtree checkout and the published repository. Verify the chosen project root exposes AGENTS.md and the project configuration. Keep this hub as the route for approvals, stale-worker checks, build slots, GPU timing and PR monitoring.

## Owner decision on merged-branch cleanup

The owner authorizes the hub to delete verified merged `task/*` branches. This supersedes the historic
blanket denial only for the guarded cleanup helper. Direct deletion commands, deletion by role workers,
other branches, advanced tips and uncertain PR or Git data remain outside the approval.

## Acceptance and review

1. A source-to-Codex inventory accounts for every tracked Claude config file, role, deny entry, workflow section and canonical helper, with an explicit disposition for nonportable local state.
2. Use TOML and command-policy parsing, non-executing policy checks, CI classifier self-tests and an isolated project-aware Codex session to verify the migration. Never execute a destructive Git operation to test a deny. Distinguish rules-file matching, CLI project discovery and desktop runtime enforcement.
3. A separate tester works from this spec and a separate reviewer checks the final mapping and restrictions. Add changelog.d/588-project-parity.md, verify relevant links and existing doc/fragment checks, and publish to the current single stack top for the owner's merge.

## Implementation verification

- Python parsed the three agent TOMLs and checked the original deny list against the rule file. The offline
  Codex CLI matched all ten deny prefixes and left six permitted examples unmatched. Each case ran through
  `codex execpolicy check`, which evaluates tokens without executing the command.
- Codex CLI `0.169.0-alpha.11.1` discovered `AGENTS.md` in isolated trusted copies of both the published and
  nested subtree layouts. CLI startup in each layout rejected a deliberately malformed scratch copy of the
  project rule file before requesting a model. Task 0569 records trusted named-agent discovery and separate
  explicit desktop dispatch. Desktop shell enforcement of the project rule file remains unverified.
- The CI classifier self-tests passed, and the source table assigns 25 test groups to six sanitizer shards.
  Native builds and remote CI are outside the implementer's local verification.
- Scratch checkouts confirmed that the copy helper skips mixed-case variants of all four protected directories,
  including source deletions and target restores, and fails for invalid checkouts without altering the target.
  The subtree publisher blocked all four staged case variants and published the changed `.gitignore` and
  Codex metadata to a local bare remote using a stand-in GitHub CLI.
- Negative controls confirmed that CI rejects each role's unapproved model or reasoning effort. Both the
  lightweight checker and native Codex rejected Unicode-prefixed and adjacent string literals in rule tokens.
  The local stack self-test passed 96 checks and the changelog/docs-fold self-test passed 93 checks.
- The copy helper preserved staged target changes and refused to write to that checkout.
- The guarded cleanup helper was tested on a disposable local bare remote with synthetic PR data. It refused
  wrong-repository metadata, protected refs, advanced tips, URL rewrites and a moved remote ref between checks
  and push. The dry run preserved all refs, and deletion removed only the verified synthetic task ref.
- Independent testing passed 32 migration checks and 22 cleanup scenarios, including rewritten URLs and
  the Windows file-only local-remote integration. It rechecked both checkout layouts, staged-owner
  publication guards, and validator failures.
- Scratch tests for the final review findings rejected the outer root, an arbitrary nested folder and
  overlapping Blendity checkouts before copy writes. The subtree publisher preserved the original index
  byte for byte on an experiment rejection and an actual staging error. The remote refs also remained byte
  for byte, and a pre-existing index lock was left to its owner. A synthetic publication confirmed
  that missing optional paths are accepted and tracked deletions are still staged.
- The file-only subtree publication suite refused each previously tracked mixed-case Claude settings,
  scheduler lock and worktree path individually without modifying the original index or remote. It accepted
  their removal and then switched the scratch checkout immediately after index unlock, before fetch.
  The remote still received the recorded, reviewed subtree.

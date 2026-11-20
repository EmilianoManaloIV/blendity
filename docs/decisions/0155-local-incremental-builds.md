# ADR 0155. Reuse verified MSVC objects for local editor builds

**Status** Accepted under the owner assignment, 2026-11-19. Related decisions include ADR 0133 and [task 0590](../tasks/0590-local-incremental-builds.md).

## Context

The Windows batch build already resolves optional Blender libraries, CRT choice, full release optimization and entry points. Its app and tests invocations compile all sources. The all target shares objects within one invocation. The Hub refreshes an editor across successive published revisions, often with most source content unchanged.

## Decision

Add an explicit local incremental entry point that consumes the existing batch build's resolved source lists, flags, target link order, libraries and deployment rules. Keep the clean path available as the parity reference. Preserve /GL and /LTCG and use an ordinary full link whenever its inputs change. A separate CMake profile would require a second configuration parity audit.

Keep persistent state per scratch worktree and configuration. Obtain transitive includes from MSVC /sourceDependencies. Key each translation unit by source path and content, included file contents, effective compiler command, tool identity, SDK and include resolution environment. Detect changes to include search candidates (including new shadowing headers). Missing, unreadable or ambiguous dependency records force recompilation. Include source additions/removals, ordered object identities, resolved library content, linker identity and flags in each target's link decision. A documentation-only revision can therefore reuse all outputs. Reject an incorrect active toolset before reading the cache.

Serialize writers per cache. Build new objects, executables and runtime files in private staging, committing successful outputs and manifests together with recoverable last-good state. Reconcile the runtime and shader inventory even when the link is current. The caller receives a verified scratch package for the existing Hub publication workflow. Running owner binaries and private projects are outside the experiment.

## Consequences and adoption

The Hub uses the incremental build for routine published-revision local Windows editor refreshes. On the owner machine a warm no-op took 19.4 seconds. The clean editor took 234.4 seconds, and an empty incremental cache took 303.4 seconds. A source edit can still incur the full LTCG link and shader warm. Task 0590 records the compiler, package and runtime comparisons and the invalidation and recovery matrix. Release, installer and CI entry points retain the stock clean build.

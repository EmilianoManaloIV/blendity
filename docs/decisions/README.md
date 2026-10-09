# Architecture decision records

Short records of decisions that shape the code: what was decided, why, and what it costs. Write one when
a change adds a subsystem, changes the data model or a documented convention, adds a dependency, or
changes how the project is built, tested or released. The human approves an ADR before the code that
depends on it is written.

Records are never rewritten after acceptance; a later decision supersedes an earlier one by number.

| ADR | Title | Status |
|---|---|---|
| [0001](0001-optional-blender-libraries.md) | Blender's libraries are optional, with dependency-free fallbacks | accepted |
| [0002](0002-gpl-licensing.md) | GPL licensing (source GPL-2.0-or-later, binaries GPL-3.0-or-later) | accepted |
| [0003](0003-copy-on-write-meshes.md) | Copy-on-write meshes and scene snapshots for undo | accepted |
| [0004](0004-sketchup-push-pull.md) | SketchUp semantics for Push/Pull | accepted |
| [0005](0005-tool-settings-in-helpers.md) | Tool settings live in the F9 panel and drag helpers | accepted |
| [0006](0006-subtree-publishing-and-prs.md) | Subtree publishing and human-merged pull requests | accepted |

## Template

```markdown
# NNNN: <decision in a few words>

- **Status:** proposed | accepted | superseded by NNNN
- **Date:** YYYY-MM-DD
- **Requirements:** R-xx, Q-xx, C-xx

## Context
The problem and the forces at play (what we must keep working, what users asked for).

## Decision
What we do, stated so code review can check it.

## Consequences
What becomes easier, what becomes harder, what we give up, what to watch.
```

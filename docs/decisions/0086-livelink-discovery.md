# ADR 0086: Live Link bridges that find Blendity - a discovery file, auto-start, a hello

- **Status:** accepted (2026-10-29, tasks 0274-0278, rounds 294-298)
- **Deciders:** the user (asked for add-ons for Unity, Unreal and Godot "so that the bridge can be automatically
  established"; chose Live Link starting with Blendity by default, a C++ editor plugin for Unreal, and to install the
  engines themselves for the in-engine tests), the main session (proposed)

## Context
ADR 0055's Live Link listened only after Start was pressed, on a port the user had to type into each bridge, and a
bridge never reconnected: every session took four manual steps, and a restarted Blendity lost every bridge. Godot and
Unreal had no bridge. Nothing shipped the bridges; they were folders in the repository.

## Decision
- **A discovery file, not a network discovery.** While Live Link listens, Blendity writes `~/.blendity/livelink.json`
  (beside its preferences) with the protocol, the port, its process id, its version, the scene and the start time, and
  removes it when it stops - only its own (by process id). A bridge reads it every 2 seconds while not connected. A file
  in the user's home is readable by every program the user runs and by no other user, needs no socket broadcast (which
  firewalls block and which would reach other computers), and survives Blendity choosing another port.
- **A stale file is a failed try.** A crash leaves the file; the bridge's connect is refused and it tries again 2 s
  later. Nothing reads the process id to decide (it may be reused).
- **Live Link starts with Blendity by default** (a preference), so the file exists whenever Blendity runs; tests and
  headless runs never start it unless asked. 40100 taken: the next of 40100-40109.
- **A hello from the bridge, read and never acted on.** A client's first line, `{"t":"client","app":...,"bridge":...}`,
  names it in the Live Link window. The link stays one way for the scene: nothing a client sends changes it.
- **Auto Connect on by default in every bridge, a typed port as the override, Disconnect holding it off** until
  Connect, so a user who wants a bridge quiet keeps it quiet.
- **Godot in GDScript, Unreal in C++ with a standard-C++ protocol layer** that Blendity's own suite compiles and replays
  on every build, so what can be checked without Unreal is checked always.
- **Packages built by a script with fixed dates; installs from Blendity copy folders** (no archive library in the
  editor) and enable the bridge where the program keeps that (Blender's preferences through Blender itself,
  project.godot, the .uproject); the packages are CI artifacts, attached to releases only by the maintainer's decision.

## Consequences
- A bridge in a program left open connects within 2 seconds of Blendity starting, and again after a restart.
- Two Blendity windows: the newest owns the file; bridges follow it. A bridge can still be pointed at the other by port.
- The Godot and Unreal bridges are unrun until the user installs those programs (tasks 0276, 0277 say so).

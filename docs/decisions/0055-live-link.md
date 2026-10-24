# ADR 0055: Live Link - a local TCP server, lines of JSON and glTF binaries, one way out

- **Status:** accepted (2026-10-22, task 0172, round 191)
- **Deciders:** the user (asked for Blender, Unity, Godot and Unreal "bridges" applying Blendity's changes in real
  time; chose Blender and Unity first, Godot and Unreal on the same protocol later, one way), the main session (proposed)

## Context
A scene made in Blendity ends up in another program: rendered in Blender, played in Unity, Godot or Unreal. Export
writes a file once; every change means exporting and importing again. The user wanted the other program to follow
the editor live, as Unity's and Unreal's Live Link plug-ins follow a motion-capture source and NVIDIA Omniverse's
connectors follow a DCC.

What the link has to carry: objects and their hierarchy, transforms, meshes (whole, and vertices moving: a drag in
Edit Mode, skinning, physics), materials (PBR and the toon fields), lights, cameras, a rig's pose, Play. What it must
not do: slow the editor's frame (a 50k-face mesh takes tens of milliseconds to write as glTF; a client can be slow or
stuck), need a library Blendity doesn't have, or open the computer to the network. Each target already reads glTF:
Blender's importer, Unity's glTFast, Godot's GLTFDocument, Unreal's Interchange.

## Decision
- **Transport: TCP on 127.0.0.1**, a server in the editor (`src/core/net_server.*`: WinSock or BSD sockets, no
  library), clients connect to it. Loopback only and no authentication: nothing off the computer can connect, and as
  the link is one way, a client can't change the scene. Port 40100 by default, any port on request.
- **Framing: one JSON object a line**, and an attachment of N bytes after the line when it ends with `"bin":N`. JSON
  because every target reads it without a package (Python, C#, GDScript, C++); binary attachments because meshes are
  megabytes and base64 would add a third. The protocol is `blendity-livelink/1` in `docs/LIVELINK.md`; clients ignore
  what they don't know, so it grows by adding.
- **Meshes as glTF 2.0 binaries** from Blendity's own exporter, in the object's space, with an attribute `_VERT` (each
  binary vertex's mesh vertex) and `extras.slot` per primitive. Every target has a glTF reader. Only positions changing
  - the common case while editing or playing - go as a **deform**: float32 positions per mesh vertex, which `_VERT`
  maps onto the binary's split vertices. glTF's axes throughout (right-handed, Y up, metres: Blendity's X mirrored), so
  a client converts as its glTF importer already does.
- **Materials as glTF material objects** (the exporter's own), with the toon fields as an extension `BLENDITY_toon`
  (every field by its Inspector name, so new fields need no protocol change) and texture slots as absolute paths
  (the client is on the same computer).
- **What changed is found by comparing**, not by hooking edits: once a frame (at most 60 times a second, and only while
  a client is connected) the link compares the scene with what it last sent - each object's name, parent, kind,
  local transform, material slots, its light's and camera's field hash, its evaluated mesh's version and topology id -
  and sends the difference. The roadmap had proposed diffing at each undo step plus a Play tick; comparing every frame
  instead carries a drag while it happens (an undo step only comes at the end), and undo, Stop and a reload need no
  code of their own: they are changes like any other. A rig's bones go together as one `pose`.
- **Nothing waits on the main thread**: each client has its own queue and sender thread; glTF binaries are built on a
  worker from a copy of the mesh; a comparison's messages go into the queues in one batch. A queued message about the
  same thing as a newer one (`transform`, `pose`, `deform`, `material`, `light`, `camera`) is replaced, never
  past a message without such a key (`object`, `mesh`, `delete`, `play`), so a slow client skips to the present, never
  sees a transform before its object, nor a newer deform ahead of a mesh. A slow client is waited for, never timed out. A client whose queue passes 256 MB is closed.

Alternatives considered:
- **USD layers (Omniverse's live layers)**: the richest model, but USD is a very large dependency and each target
  would need USD too (Unity and Godot don't ship it).
- **WebSocket**: would let a browser connect; it adds an HTTP upgrade and frame masking for no target the user asked
  for. The line framing can be carried inside it later.
- **A binary schema (FlatBuffers, protobuf)**: smaller messages, but a generated reader in every language; JSON lines
  are cheap enough at 60 comparisons a second (the stress section has the numbers).
- **Hooking `commit_undo`** (the roadmap's design): see above; it loses the live part of a drag.

## Consequences
- A bridge is small: read lines, read attachments, import glTF, apply transforms (`tools/livelink_dump.py` is the
  reading loop in 50 lines of Python). Blender's and Unity's bridges are tasks 0173 and 0174; Godot's and Unreal's
  consume the same protocol (task 0175).
- One way only: edits on the other side aren't sent back, and the next change in Blendity to the same thing overwrites
  them. A two-way link would need conflict rules; nobody asked for it.
- The comparison costs the frame in proportion to the objects while a client is connected (the stress section
  `livelink` measures 1,000 moving objects), nothing without one.
- Skinned meshes go as deforms of their evaluated mesh (12 bytes a vertex a comparison), not as a skin with weights:
  every client can show them without a skinning implementation, at the cost of bandwidth on big characters. Bones are
  sent too (`pose`), so a bridge that wants its own skin can build one later.
- What a client sees is the viewport's mesh (modifiers applied), not the modifier stack: Blendity's modifiers have no
  equivalent in most targets.

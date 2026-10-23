# 0172: Live Link - the server and the protocol

- **Status:** done (round 191)
- **Requirements:** R-06 (file IO with Blender and the engines), R-01 (the editor)
- **Decisions:** ADR 0055 (a local TCP server, lines of JSON and glTF binaries, one way out)
- **Model:** main session
- **PR:** #136 (stacked on #135)

## Goal
The user: Blender, Unity, Godot and Unreal "bridges" that apply live changes from Blendity in real time; Blender and
Unity first, Godot and Unreal on the same protocol; one way. This task is the editor's side and the protocol; the
Blender bridge is 0173, Unity's 0174, Godot's and Unreal's 0175.

## Design
- `src/core/net_server.{h,cpp}`: a TCP server on 127.0.0.1 (WinSock: `ws2_32` in build.bat; BSD sockets elsewhere),
  an accept thread, a sender thread and a queue per client, keyed messages replaced in place (O(1): a key's place by
  sequence number, a barrier after each message without a key), a 256 MB queue limit, a batch send. `LineClient` for
  the tests and tools.
- `JsonWriter` in `src/core/json.h` (one line; floats by `to_chars`, shortest round trip).
- `src/editor/livelink.{h,cpp}`: `livelink::Link` - per tick (every frame; it compares at most every 1/60 s, nothing
  without a client) new clients get `hello`, `sync`, every material / object / light / camera and `sync_end`, the
  meshes from a worker; the others get what changed: `object` (structure), `transform`, `pose` (a rig whole),
  `deform` (positions, when the topology id holds), `mesh` (a glTF binary built on the worker from a copy), `material`
  (glTF's object, `BLENDITY_toon`, map paths), `light`, `camera`, `delete`, `play`. Messages about the same thing
  replace each other in a client's queue; a mesh built from an older copy is followed by a deform.
- The exporter: `extras.slot` on each glTF primitive, `_VERT` with `ExportOptions::vertex_ids`, `BLENDITY_toon` on Toon
  materials (also in exported files), `gltf_material_json`, `reflect_to_json` (scene.h).
- The editor: Window > Live Link (port, Start / Stop, clients, counts, the comparison's time), console `livelink
  start [port] | stop | status`, `livelink_tick()` in `frame()` after the scene's changes.
- `docs/LIVELINK.md` (the protocol), `tools/livelink_dump.py` (a 50-line client that prints the stream).

The roadmap's design hooked `commit_undo` to diff each undo step against the previous; the link compares every frame
instead, so a drag goes out while it happens and undo / Stop / reload need nothing of their own (ADR 0055).

## Acceptance criteria
1. A client gets `hello`, then `sync` with the scene's object count, an `object` for each, and each mesh as a valid
   glTF binary (magic, JSON chunk, index count = 3 x the mesh's triangles, `_VERT` for every vertex).
2. Moving one object sends one `transform` (glTF's axes); a material edit one `material` with its colour; nothing
   changed, nothing sent; moved vertices one `deform`, new faces one `mesh`; a posed rig one `pose`; a deleted object
   one `delete`.
3. In Play at most 60 transforms a second, in order, the last where the object ended.
4. Two clients get the same stream; a client that leaves mid-send is dropped and the others go on; the link takes a
   new client after.
5. The cost: the frame with the link off, on without a client, with one and two clients while 1,000 objects move, a
   big mesh's sync, deforms every frame, a client leaving mid-send - measured (stress `livelink`).
6. The Linux build links unchanged (BSD sockets, no flag).

## Results
The stress section `livelink` on the RTX 4070 SUPER PC (quiet machine):

- A frame (mean / most, ms) with the link off 11.49 / 13.14, on without a client 11.46 / 12.93, with one client draining 12.50 / 14.16, with two 13.10 / 31.04.
- Comparing the scene with what was sent (1,000 moved objects: 1,000 transforms) 1.23 / 1.50 ms mean / most with one client, 1.42 / 3.50 with two; 60240 and 120491 messages, 5.49 and 10.98 MB in the 120 frames.
- A client's sync with a 95232-face mesh: the frames at most 11.42 ms; the worker built 1005 glTF binaries in 182 ms; the sync took 777 ms of frames.
- A vertex of the 95232-face mesh moved every frame: 14.57 ms a frame, 64 deforms, 69.76 MB (95234 vertices: 1.09 MB a deform).
- A client that leaves while a mesh is going out: the frames at most 2.17 ms over the next 30 frames; 0 client(s) left connected.
- One client cost the frame 1.00 ms on average against the link off (12.50 vs 11.49 ms; the views' rendering is most of a frame).

The Blender bridge (task 0173) replays tests/fixtures/livelink_stream.bin and connects live (tested with Blender 5.2.2).

## Tests
tests/test_livelink.cpp (group `livelink`): "json: JsonWriter ...", "net: the server's lines and attachments ...",
"livelink: a client gets hello ...", "... one transform ...", "... one deform ...", "... one pose ... one delete",
"... in Play at most 60 transforms a second ...", "... two clients ...", "export: glTF primitives name their slot ...".

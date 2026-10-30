# 0274: Live Link starts with Blendity and says where it listens

- **Status:** done (round 294)
- **Requirements:** R-10
- **Decisions:** ADR 0086
- **Model:** main session
- **PR:** #229

## Asked
The user (2026-10-29): "make add-ons for Unity, Unreal and Godot so that the bridge can be automatically established
(Unity package, Blender package, etc.)". Their choice: Live Link starts with Blendity by default, as a preference.

## What changed
- **Start with Blendity** (Preferences > Live Link, and a checkbox in the Live Link window; on by default; saved with
  the port). A headless run or a test starts it only when `BLENDITY_LIVELINK_AUTOSTART=1`.
- **The discovery file**: while Live Link listens, `~/.blendity/livelink.json` (beside the preferences;
  `BLENDITY_PREFS_DIR` moves both) holds the protocol, the port, the process id, the app, its version, the open scene
  and when it started. It is written again when another scene opens, and removed on Stop and when Blendity ends, but
  only when it is this process's (another Blendity started since owns it). It is written whole and then moved into
  place, so a bridge never reads half of it. The tests point `BLENDITY_PREFS_DIR` at a scratch folder: a running
  Blendity's file is never touched by them.
- **A taken port**: 40100 busy, the next free of 40100-40109 is used and the window says which.
- **Clients by name**: a bridge's first line, `{"t":"client","app":"Unity 6000.0","bridge":"1.1"}`, is kept by the
  transport (everything else a client sends is still read and dropped) and the window lists "Connected: Unity 6000.0
  (bridge 1.1)".
- **`tools/livelink_fake_server.py`**: a stand-in Live Link serving the recorded session and writing the discovery file,
  so every bridge's auto-connect test runs without Blendity.

## Tests (`tests_round294`, Live Link group)
Headless with the preference on: not started. With 40100 held by another server: listening on 40101, said; the file's
protocol, port, process and app right; a client's hello listed by name; the file gone after Stop and after the
editor's end. Another Blendity's file left alone by Stop.

## Not done
- Two Blendity windows: the second (on 40101) owns the file; when it closes, the first's port isn't written back, so
  the bridges find the first again only when its Live Link restarts or a scene opens in it.
- The client's hello is read when its queue is idle: a client that connects during a long first sync is named a moment
  later.

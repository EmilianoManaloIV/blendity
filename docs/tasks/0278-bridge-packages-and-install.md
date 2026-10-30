# 0278: the bridges as packages, and installed from Blendity

- **Status:** done (round 298)
- **Requirements:** R-10
- **Decisions:** ADR 0086
- **Model:** main session
- **PR:** #229

## Asked
Task 0274's request: "Unity package, Blender package, etc." - a file to install for each program, and as little to do
by hand as possible.

## What changed
- **`tools/package_bridges.py`** makes five files with fixed dates (the same sources, the same bytes): the Blender
  extension zip, the Unity package as a .tgz and as a .unitypackage (into Assets/BlendityLiveLink), the Godot add-on zip
  (addons/blendity_livelink) and the Unreal plugin zip (with its automation test's session). `release.yml` builds them
  on every run as the run's artifacts; attaching them to a release stays the maintainer's decision.
- **The installers carry `bridges/`** (Windows, Linux, macOS), with the Unreal plugin's session.
- **Install a Bridge** in the Live Link window (and the palette's "Install a Bridge..."):
  - Blender: found (Program Files' newest, the PATH, /Applications, or `BLENDITY_BLENDER`); on a thread of its own its
    version asked when the folder doesn't say it, the add-on copied into its extensions folder and enabled by Blender
    itself (`--python-exit-code 1`, the enable's result asserted, its preferences saved), a toast when done; a second
    click meanwhile is refused.
  - Unity, Godot, Unreal: a folder picker for the project; the bridge copied into its Packages / addons / Plugins;
    Godot's `project.godot` and Unreal's `.uproject` enable it (a text edit that keeps the file's layout and the other
    plugins; an Unreal entry there but off turned on; a `[editor_plugins]` line ending the file handled). A folder that
    isn't such a project is refused, saying why.
  - Console: `livelink install blender | unity|godot|unreal <folder>`.

## Tests
- `tests_round298` (Live Link group): scratch Unity, Godot and Unreal projects - the package with its .meta files; the
  add-on enabled beside another plugin and in a new section, not twice; the plugin in a Plugins list and in a new one,
  both still JSON; a folder that is no project refused; the window's four buttons, an engine's asking for a folder.
- With `BLENDITY_TEST_BLENDER` (here: Blender 5.2.2, `BLENDER_USER_RESOURCES` in a scratch folder): copied, enabled by
  Blender, its preferences saved.
- `package_bridges.py` twice: identical bytes; Blender's `extension validate` accepts the zip.

## Not done
- Unity's, Godot's and Unreal's own runs of what was installed: after the user installs them.

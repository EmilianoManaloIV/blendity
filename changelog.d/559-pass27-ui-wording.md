## 2026-11-17 (round 559): Pass 27's UI wording - "color", and shortcuts read from the keymap

**Asked:** the hub, from M3's HCI sweep over Pass 27 (verify-544). Two static audits rose against main: consistency
0 -> 11 and labels 1 -> 4. Put them back, and run them in CI.

**Fixed (task 0541)**
- **"color":** the interface's text says "color" where Pass 27 wrote "colour". This covers the point cloud's tooltips and help,
  Vertex Paint's tooltips and help, Paint's brush help and its "Unified Color" switch (was "Unified Colour"), and the material's
  vertex-color help. The console still accepts `colour` as a word.
- **Shortcuts from the keymap:** the Animation window's Key menu names the chords the keymap gives. Convert to Quaternion's tooltip
  names `pose.rotation_convert`'s chord. Paste Pose's names Adjust Last's: F9 in most presets, Ctrl+F9 in Maya's, whose F9 is
  Vertex mode (the old hard-coded "F9" was wrong there).
- **Clipped labels (part 2):** at 1024 x 768 / x1.5 words were cut with "..." on Pass 27's panels. The layout gives way, not the
  words:
  - a dropdown whose longest word does not fit beside its label goes on a row of its own under it (the Inspector's enumerations:
    point cloud Type "Gaussian Splats", camera Panorama Type "Equirectangular", Weight Paint's Brush "Add Weight" and Falloff
    "Smooth");
  - a switch whose name is longer than the label column ("Front Faces Only", "Unified Strength") takes the room it needs and its
    box moves after it;
  - Vertex Paint's toggle ("Exit Vertex Paint") and the Rigging button ("Vertex Groups, Weight Paint: Rigging...") are sized to
    their words and take a row each when one row cannot hold both; New Point / New Corner Layer wrap one under the other;
  - the Animation window's physics tools (Center of Mass ... AutoPosing, Ragdoll, Apply) wrap button by button, so AutoPosing is
    whole and Ragdoll no longer runs off the window.

**Changed**
- `tools/hci_label_audit.py` skips comment lines: a comment quoting a test's name is not a label anyone sees.
- CI's `linux-unit` runs `hci_consistency_audit.py --check` and `hci_label_audit.py --check`, so a round that writes
  "colour" in a tooltip or a chord by hand fails its own PR.

**Checked:** both suites - with Blender's libraries CHECKS_LIBS; dependency-free CHECKS_W (Windows) and CHECKS_L (Linux);
both audits at 0.

<!-- docs/USABILITY.md -->
after: | With AutoPosing on: a Puppet handle dragged, Ctrl+Z
| Pass 27's new panels said "colour" where the rest of the interface says "color", and two Animation window tooltips named Ctrl+R and F9 by hand - wrong in Maya's preset, where Adjust Last is Ctrl+F9. (round 559) | The text says "color"; the tooltips name the keymap's own chords; CI runs both audits so it stays that way. |
| Labels cut with "..." on Pass 27's panels at 1024 x 768 (point cloud Type, camera Panorama Type, Vertex Paint, Weight Paint's brush), and the Animation window's third toolbar row cutting AutoPosing and losing Ragdoll off the window at x1.5. (round 559) | Dropdowns and switches that do not fit beside their label take a row or room of their own, Vertex Paint's buttons wrap, the physics tools wrap one by one; the words are unchanged. |

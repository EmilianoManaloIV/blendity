## 2026-11-27 (round 668): Keep Unity material edits when gloss finishes loading

**Asked:** A background texture measurement reset an edited Metallic value on a Unity material.

**Changed (task 0650, ADR 0157):** When a measured gloss value arrives, each affected material takes the new roughness only if it still shows the provisional value. Other fields and locally edited roughness are preserved in the current scene, undo and redo states and the scene restored after Play. Reimport Unity Materials still reloads the source file.

**Checks:** The baseline reproduction saved Metallic 0.37 and then read back 1.0 after background completion. Regression fixtures cover two materials sharing a map, directly edited roughness, known and worker-supplied measurements, explicit reimport, saved scene values, Undo, Redo and Stop.

<!-- docs/USABILITY.md -->
append:
### Round 668: Unity material edits during gloss loading

An automatic gloss measurement preserves edited material fields. Reimport Unity Materials explicitly rereads the source.

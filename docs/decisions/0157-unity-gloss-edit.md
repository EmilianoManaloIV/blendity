# 0157: Merge measured Unity roughness into each material state

- **Status:** accepted for implementation under the owner-authorized Pass 37 repair, task 0650.
- **Date:** 2026-11-27
- **Requirements:** R-04, Q-06.

## Context
ADR 0131 decision 3 replaced every field in a requesting Unity material and its Undo, Redo and Play copies when a background texture measurement finished. Task 0643 reproduced a saved Metallic edit changing from 0.37 to 1.0 although the measurement only supplied roughness.

## Decision
For automatic gloss completion, compare each material state's roughness with the provisional value produced by the source shader using its missing-mean fallback. A state still showing that value takes the measured roughness. A different value records a local choice and survives. Setting a field back to precisely its provisional value makes it eligible again. Every other material field retains its value in that state. Check that the source still uses a texture whose measurement has completed.

This supersedes the background-completion portion of ADR 0131 decision 3. Explicit Reimport Unity Materials, completed package imports and fresh-session reads continue to take the source file's complete interpretation. Blendity keeps the Unity source file intact. The same rules apply to several materials sharing a texture and to means made available before a waiting request is polled.

## Consequences
Undo or Redo can restore an earlier edited value while a material whose roughness was still provisional benefits from the measurement. Returning from Play brings back the edit-mode state's own fields. A saved scene records the values visible at save time; reopening a foreign material still uses its source and the cached measurement.

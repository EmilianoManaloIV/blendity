# 0070: A global sequencer - each bar a clip and the object it plays on

- **Status:** done
- **Requirements:** R-06
- **Decisions:** ADR 0030 (amended)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/63

## Goal
The user (2026-10-14): "The sequencer should work globally and have two things to consider when adding a clip. Which
animation clip it is using and what object is it attributed to."

## Scope
- In: the scene owns one sequence (`Scene::sequence`, saved with the scene); the Sequencer window edits it without a
  selection; each bar names its clip and its object (`SeqInstance::target`, a 4th column in `.seq`; older files load
  with none, the Sequencer component's own object); dropping a clip takes the selected character as its object; the
  Object field below the tracks takes a Hierarchy drag; `seq add <clip> <track> <start> [object]`, `seq object`;
  Preview poses every bar's object; in Play the scene's director (a Sequencer of the scene's) plays it, triggers and
  `seq jump` included; two overlapping bars crossfade only when they play on the same object.
- The Sequencer component stays: a per-object player (its bars without an object play on it).

## Acceptance criteria
1. One scene sequence moves two objects at once from bars on two tracks, in Preview and in Play.
2. Overlapping bars on different objects don't blend.
3. The scene file keeps its sequence; the `.seq` keeps each bar's object.

## As built
- Objects are found by name, as other references: renaming an object leaves its bars playing on nothing, and two
  objects of one name take the first. The bar's label shows the name it plays on.
- A bar with no object plays on the selected object in Preview, and on nothing from the scene's sequence in Play
  (a Sequencer component's sequence plays such bars on its own object).
- A scene without its own sequence shows the selected object's Sequencer component sequence (made before this task),
  so those stay editable.
- An object's Animation component steps aside in Play while the scene's sequence has a bar on it, wherever the bar is.
- Bars on different objects can overlap on one track: each object's own last two crossfade.
- An object given a bar during Preview is put back with the rest when Preview ends.
- The scene file keeps the sequence's path quoted, so renaming the `.seq` keeps the scene (and its undo steps) on it.
- Tests: rounds 95 and 96.

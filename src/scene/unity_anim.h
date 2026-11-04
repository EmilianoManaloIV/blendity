// SPDX-License-Identifier: GPL-2.0-or-later
// Unity's .anim files (round 407, task 0384; ADR 0102): an AnimationClip's YAML read as a Blendity clip and a Blendity
// clip written as one. Positions, Euler rotations, scales and floats (blend shapes, Active) with their tangents; the
// paths are Unity's child paths, which Blendity's tracks use too. A Unity project's file is never written over: a clip
// read from one saves its edits to a Blendity copy beside it (save_dirty_clip_assets), and `export anim` writes a new
// Unity file.
#pragma once
#include "animation.h"
#include <string>
#include <vector>

namespace bl {

struct GameObject;

/* True when `text` is a Unity YAML file (its "%YAML" header), as against Blendity's own clip text. */
bool is_unity_anim_text(const std::string &text);

/* A Unity .anim's AnimationClip document as a Blendity clip. Per-axis editor curves (m_EditorCurves' m_LocalPosition /
 * m_LocalScale, m_EulerEditorCurves' localEulerAnglesRaw) are taken first - each axis with its own keys and modes - and
 * m_PositionCurves / m_EulerCurves / m_ScaleCurves for paths without them; m_RotationCurves (quaternions) -> Rotation
 * when a path has neither (Unity's Euler order, the turn nearest the key before); m_FloatCurves: blendShape.<name> ->
 * MeshRenderer.BlendShape/<name> (0-100 -> 0-1), m_IsActive -> Active. m_SampleRate the frame rate, keys at time x
 * rate, slopes per second made per frame; a span is constant when its first key's out slope or its second key's in slope
 * is infinite (Unity's rule), linear when both modes say so, free with the slopes when broken or free, else auto.
 * m_StopTime the length, m_LoopTime the loop. What it can't carry - material and script floats, Humanoid muscles,
 * object curves, events, a legacy flag - goes to `notes`, one line a kind, and sets `lossy` when given (the clip's
 * document holds more than the clip). False with why when the text isn't an AnimationClip. */
bool unity_anim_to_clip(const std::string &text, AnimationClip &out, std::vector<std::string> *notes, std::string *why, bool *lossy = nullptr);

/* A Blendity clip as a Unity .anim (one AnimationClip document, fileID 7400000): each vector property's axes as one
 * runtime curve at the union of their frames - an axis without a key there takes its curve's value and slope, mode
 * free - and as per-axis editor curves with each axis's own keys; an axis without a track takes the object's current
 * value from `root` when given (a scale axis 1 otherwise, a position or rotation axis 0, said in `notes`); rotations as
 * m_EulerCurves with localEulerAnglesRaw editor curves in m_EditorCurves, as Unity's own files keep them (no quaternion
 * curves: Unity's Euler-interpolated clips have none); floats as
 * m_FloatCurves and m_EditorCurves (blendShape.<name> on the Skinned Mesh Renderer in 0-100 with the slopes scaled,
 * m_IsActive on the GameObject). A constant span writes infinite slopes on both of its keys, a linear span its secant
 * on both; tangent modes as Unity's packed left / right modes. Tracks of other properties are left out and said. */
std::string clip_to_unity_anim(const AnimationClip &c, const GameObject *root = nullptr, std::vector<std::string> *notes = nullptr);

/* Export Unity Package's view of a .anim file's bytes: Blendity's clip text converted to Unity's (named for the file),
 * anything else as it is. `converted` set when it was; `dropped` counts the tracks that couldn't go. */
std::string unity_package_clip_data(const std::string &data, const std::string &pathname, bool *converted, int *dropped, int *zeroed = nullptr);

/* A fresh .anim's .meta body for Unity (NativeFormatImporter, the clip as the main object). */
std::string unity_anim_meta(const std::string &guid);

}  // namespace bl

// SPDX-License-Identifier: GPL-2.0-or-later
// The module registry (task 0235, ADR 0073): one row per window kind - its title, icon, group, the stages it belongs to,
// whether a second window may hold one, whether its group folds, its Window menu shortcut - read by the tab strips, the
// Window menu, Add Tab, the palette, the Wheel's modes and the UI file. A new window kind is a row here, a case in
// dock_draw_window, its dock id in workspaces.cpp's comment and the coverage map's table.
#include "editor.h"

namespace bl {

namespace {

using ui::Icon;
constexpr uint32_t kAlways = Editor::FocusAll;
constexpr uint32_t kModel = Editor::FocusCreate | Editor::FocusDraw | Editor::FocusEdit | Editor::FocusCheck | Editor::FocusSnap | Editor::FocusDeform;
constexpr uint32_t kDrawing = Editor::FocusDraw | Editor::FocusReference;

/* The order is WindowKind's: the dock strings' ids are these indices (workspaces.cpp). `folds`: in one leaf the kinds
 * of such a group fold into one tab with a strip of members under it. The shortcuts are keymap action ids: the Window menu shows the preset's chord (task 0254). */
const Module kModules[] = {
    {WindowKind::Scene, "Scene", Icon::Grid, "Views", kAlways, true, false, "window.scene"},
    {WindowKind::Game, "Game", Icon::Play, "Views", kAlways, true, false, "window.game"},
    {WindowKind::Hierarchy, "Hierarchy", Icon::Menu, "Scene", kAlways, false, false, "window.hierarchy"},
    {WindowKind::Inspector, "Inspector", Icon::Info, "Scene", kAlways, true, false, "window.inspector"},
    {WindowKind::Project, "Project", Icon::Folder, "Assets", kAlways, false, false, "window.project"},
    {WindowKind::Console, "Console", Icon::Terminal, "Output", kAlways, false, false, "window.console"},
    {WindowKind::Learn, "Learn", Icon::Book, "Learn", kAlways, false, false, "window.learn"},
    {WindowKind::Research, "Research", Icon::Flask, "Learn", kAlways, false, false, "window.research"},  // (retired, task 0034)
    {WindowKind::Profiler, "Profiler", Icon::Chart, "Tools", kAlways, false, true, "window.profiler"},
    {WindowKind::Render, "Render", Icon::Camera, "Output", Editor::FocusLight | Editor::FocusMaterial, false, false, "window.render"},
    {WindowKind::UVEditor, "UV Editor", Icon::Face, "Views", Editor::FocusUV, false, false, "window.uv"},
    {WindowKind::Materials, "Materials", Icon::Eye, "Shading", Editor::FocusMaterial, false, true, nullptr},
    {WindowKind::Tools, "Modeling Tools", Icon::Vertex, "Tools", kModel, false, true, nullptr},
    {WindowKind::Lighting, "Lighting", Icon::Lightbulb, "Shading", Editor::FocusLight, false, true, nullptr},
    {WindowKind::Textures, "Textures", Icon::Paper, "Shading", Editor::FocusMaterial, false, true, nullptr},
    {WindowKind::Rigging, "Rigging", Icon::Move, "Rigging & Animation", Editor::FocusRig, false, true, nullptr},
    {WindowKind::Animation, "Animation", Icon::Step, "Rigging & Animation", Editor::FocusRig, false, true, nullptr},
    {WindowKind::Sequencer, "Sequencer", Icon::Play, "Rigging & Animation", Editor::FocusRig, false, true, nullptr},
    {WindowKind::LiveLink, "Live Link", Icon::Globe, "Tools", Editor::FocusRig, false, true, nullptr},
    {WindowKind::Editing, "Editing", Icon::Eye, "Output", Editor::FocusMaterial, false, false, nullptr},
    {WindowKind::MyTools, "My Tools", Icon::Grid, "Yours", kAlways, false, false, nullptr},
    {WindowKind::Draw, "Draw", Icon::Paper, "Views", kDrawing, false, false, nullptr},
    {WindowKind::Layers, "Layers", Icon::Menu, "Scene", kDrawing, false, false, nullptr},
    {WindowKind::History, "History", Icon::Step, "Scene", kAlways, false, false, "window.history"},  // (task 0254)
};
static_assert(sizeof(kModules) / sizeof(kModules[0]) == (size_t)WindowKind::Count, "a module per window kind");

}  // namespace

const Module &module_of(WindowKind k) { return kModules[(int)k]; }

const Module *modules(int &count) {
  count = (int)WindowKind::Count;
  return kModules;
}

const char *window_title(WindowKind k) { return module_of(k).name; }
ui::Icon window_icon(WindowKind k) { return module_of(k).icon; }

}  // namespace bl

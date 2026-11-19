// SPDX-License-Identifier: GPL-2.0-or-later
#include "editor.h"

#include "../core/jobs.h"

#include <algorithm>

namespace bl {

void Editor::source_import_dialog() {
  if (dialog_ == Dialog::SourceImport) return;
  source_picker_cancel();
  source_picker_.project = project_root_;
  source_picker_.scene_serial = scene_ ? scene_->serial : 0;
  dialog_ = Dialog::SourceImport;
  ui_.redraw = true;
}

void Editor::source_picker_cancel() {
  if (source_picker_job_) source_picker_job_->discarded = true;
  source_picker_ = {};
}

void Editor::source_picker_start(SourcePickerJob::Kind kind, const std::string &path) {
  if (dialog_ != Dialog::SourceImport || source_picker_job_) return;
  auto &picker = source_picker_;
  auto job = std::make_unique<SourcePickerJob>();
  job->kind = kind;
  job->path = path;
  if (kind != SourcePickerJob::Choose) job->content = picker.content;
  if (kind == SourcePickerJob::Read || kind == SourcePickerJob::Prepare) {
    if (picker.selected < 0) return;
    job->selected = picker.entries[(size_t)picker.selected];
  }
  if (kind == SourcePickerJob::Prepare) {
    if (!picker.ready || import_job_ || ipv_.open) return;
    job->result = std::move(picker.result);
    job->stage.project = picker.project;
    picker.ready = false;
  }
  SourcePickerJob *raw = job.get();
  auto work = [raw] {
    if (raw->kind == SourcePickerJob::Prepare) {
      raw->ok = stage_source_import_files(raw->result, raw->stage, raw->error);
    }
    else if (raw->kind == SourcePickerJob::Read) {
      raw->ok = import_source_entry(raw->content, raw->selected, raw->result);
      if (raw->ok && raw->result.nodes.empty()) {
        raw->ok = false;
        raw->result.error = "The Source entry contains no objects: " + raw->path;
      }
      raw->error = raw->result.error;
    }
    else {
      raw->ok = raw->content.mount(raw->path, raw->error, raw->selected_path);
      if (raw->ok && raw->kind == SourcePickerJob::Choose) {
        raw->entries = raw->content.entries();
        if (!raw->selected_path.empty()) raw->ok = raw->content.locate(raw->selected_path, raw->selected, raw->error);
      }
    }
    raw->done.store(true, std::memory_order_release);
  };
  if (jobs_async()) raw->thread = std::thread([work] { JobSystem::global().run_background(work); });
  else work();
  source_picker_job_ = std::move(job);
  ui_.redraw = true;
}

void Editor::source_picker_poll() {
  if (!source_picker_job_) return;
  ui_.redraw = true;
  if (!source_picker_job_->done.load(std::memory_order_acquire)) return;
  if (!source_picker_job_->discarded && dialog_ == Dialog::SourceImport &&
      source_picker_job_->kind == SourcePickerJob::Prepare && (any_button_down() || import_job_ || ipv_.open)) return;
  auto job = std::move(source_picker_job_);
  if (job->discarded || dialog_ != Dialog::SourceImport) return;

  auto &picker = source_picker_;
  if (!scene_ || picker.scene_serial != scene_->serial || picker.project != project_root_) {
    source_picker_cancel();
    dialog_ = Dialog::None;
    ui_.close_popups();
    toast(LogLevel::Warning, "Source import canceled because the project or scene changed.");
    return;
  }
  picker.diagnostics.clear();
  if (!job->ok) {
    picker.diagnostics.push_back(job->error.empty() ? "Could not read Source content: " + job->path : job->error);
    picker.diagnostics.insert(picker.diagnostics.end(), job->result.notes.begin(), job->result.notes.end());
    return;
  }
  if (job->kind == SourcePickerJob::Prepare) {
    ImportFiles files;
    if (!publish_source_import_files(job->selected, job->result, job->stage, files, job->error)) {
      picker.diagnostics = {job->error};
      return;
    }
    const std::string path = job->selected.virtual_path;
    dialog_ = Dialog::None;
    ui_.close_popups();
    source_picker_cancel();
    ScopedTimer started;
    const bool map = fs::extension(path) == ".bsp" || fs::extension(path) == ".vmap_c" || fs::extension(path) == ".vwrld_c";
    // A map keeps its Source origin so separately imported props line up in world space.
    const std::function<void(GameObject *)> position = map ? std::function<void(GameObject *)>([](GameObject *root) {
      if (root) root->set_world_position(Vec3{});
    }) : std::function<void(GameObject *)>{};
    finish_import(path, false, job->result, started, ImportOutline::Keep, 0.0f, false, position, &files);
    return;
  }
  if (job->kind == SourcePickerJob::Read) {
    picker.diagnostics = job->result.notes;
    picker.result = std::move(job->result);
    picker.ready = true;
    if (picker.diagnostics.empty()) source_picker_start(SourcePickerJob::Prepare, {});
    return;
  }

  picker.content = std::move(job->content);
  picker.ready = false;
  picker.result = {};
  if (job->kind == SourcePickerJob::AddRoot) {
    if (std::find(picker.roots.begin(), picker.roots.end(), job->path) == picker.roots.end()) picker.roots.push_back(job->path);
    picker.root_text.clear();
    return;
  }

  picker.primary_text = job->path;
  picker.roots = {job->path};
  picker.root_text.clear();
  picker.entries = std::move(job->entries);
  picker.selected = picker.entries.size() == 1 ? 0 : -1;
  if (!job->selected_path.empty()) {
    const auto selected = std::find_if(picker.entries.begin(), picker.entries.end(), [&](const SourceEntry &entry) {
      return entry.virtual_path == job->selected.virtual_path && entry.provider == job->selected.provider;
    });
    if (selected != picker.entries.end()) picker.selected = int(selected - picker.entries.begin());
  }
  if (picker.entries.empty())
    picker.diagnostics.push_back("No supported entries in this item. Choose a folder or VPK containing .mdl, .bsp, .vmdl_c, .vmap_c or .vwrld_c files.");
}

void Editor::draw_source_import_dialog(const std::function<void()> &close) {
  auto &u = ui_;
  auto &picker = source_picker_;
  const bool idle = !source_picker_job_;
  const auto browse = [this](bool folder, bool add) {
    defer([this, folder, add] {
      if (dialog_ != Dialog::SourceImport || source_picker_job_) return;
      const std::string where = source_picker_.primary_text;
      const std::string start = where.empty() ? project_root_ : (fs::is_dir(where) ? where : fs::parent(where));
      std::string path;
      const std::vector<platform::FileFilter> filters = add
          ? std::vector<platform::FileFilter>{{"Valve Package", {".vpk"}}}
          : std::vector<platform::FileFilter>{{"Source files", {".mdl", ".bsp", ".vmdl_c", ".vmap_c", ".vwrld_c", ".vpk"}}};
      const PathPick choice = folder ? pick_folder(add ? "Add a Source Content Root" : "Choose a Workshop Folder", start, path)
                                     : pick_open_path(add ? "Add a VPK" : "Choose a Source Model, Map or VPK", start, filters, path);
      if (choice == PathPick::Chosen) source_picker_start(add ? SourcePickerJob::AddRoot : SourcePickerJob::Choose, path);
      if (choice == PathPick::Unavailable) source_picker_.diagnostics = {"Enter the local path in the dialog to choose it."};
    });
  };
  auto path_row = [&](std::string &path, bool add) {
    const Recti r = u.popup_row().shrink(u.px(2));
    const int bw = u.px(92), gap = u.px(4);
    const Recti field{r.x, r.y, r.w - 3 * (bw + gap), r.h};
    const char *key = add ? "Source root path" : "Source item path";
    if (u.record_buttons) u.button_rects[key] = field;
    if (idle) u.text_field(u.id(key), field, path, nullptr, add ? "Dependency folder or VPK path" : "Installed item path");
    else u.label(field, path, u.theme.text_dim);
    const Recti use{field.right() + gap, r.y, bw, r.h};
    const Recti folder{use.right() + gap, r.y, bw, r.h};
    const Recti file{folder.right() + gap, r.y, bw, r.h};
    if (u.button_enabled(use, add ? "Add Root" : "Use Path", idle && !path.empty(), idle ? "Enter a local path" : "Reading Source content"))
      source_picker_start(add ? SourcePickerJob::AddRoot : SourcePickerJob::Choose, path);
    if (u.button_enabled(folder, add ? "Add Folder..." : "Choose Folder...", idle, "Reading Source content")) browse(true, add);
    if (u.button_enabled(file, add ? "Add VPK..." : "Choose File...", idle, "Reading Source content")) browse(false, add);
  };
  path_row(picker.primary_text, false);

  const int rh = u.row_h();
  const int n = int(picker.entries.size());
  if (n) {
    Recti list = u.popup_row(rh * std::min(n, 6));
    list.x += u.px(4);
    list.w -= u.px(8);
    const int offset = u.begin_scroll(u.id("source_entries"), list, n * rh);
    for (int i = std::max(0, offset / rh); i < n; ++i) {
      const auto &entry = picker.entries[(size_t)i];
      const Recti row{list.x, list.y + i * rh - offset, list.w - u.px(12), rh};
      if (row.y >= list.bottom()) break;
      const bool duplicate = (i > 0 && picker.entries[(size_t)i - 1].virtual_path == entry.virtual_path) ||
                             (i + 1 < n && picker.entries[(size_t)i + 1].virtual_path == entry.virtual_path);
      std::string label = entry.virtual_path;
      if (duplicate) {
        const auto chain = entry.provider.find(" > ");
        const std::string archive = chain == std::string::npos ? fs::filename(entry.provider) : entry.provider.substr(chain + 3);
        label += " (" + archive + ")";
      }
      if (u.record_buttons) u.button_rects["source:entry " + entry.virtual_path + " @ " + entry.provider] = row;
      u.push_id(u.id(entry.provider));
      const bool selected = u.button_enabled(row, label, idle, "Reading Source content", i == picker.selected);
      u.pop_id();
      if (selected) {
        if (picker.selected != i) {
          picker.ready = false;
          picker.result = {};
          picker.diagnostics.clear();
        }
        picker.selected = i;
      }
      u.tooltip(entry.provider);
    }
    u.end_scroll();

    const Recti provider = u.popup_row().shrink(u.px(4));
    const std::string text = picker.selected >= 0 ? picker.entries[(size_t)picker.selected].provider : "Select a model or map.";
    u.label(provider, text, u.theme.text_dim);
    u.tooltip(text);
  }

  if (!picker.roots.empty()) {
    path_row(picker.root_text, true);
    if (picker.roots.size() > 1) {
      const Recti roots = u.popup_row().shrink(u.px(4));
      u.label(roots, std::to_string(picker.roots.size() - 1) + " additional content roots", u.theme.text_dim);
      std::string labels;
      for (const auto &root : picker.roots) labels += (labels.empty() ? "" : "\n") + root;
      u.tooltip(labels);
    }
  }

  const auto &warnings = picker.content.mount_warnings();
  if (!idle) {
    const Recti row = u.popup_row().shrink(u.px(4));
    const char *status = source_picker_job_->kind == SourcePickerJob::Read ? "Reading Source resources..."
                         : source_picker_job_->kind == SourcePickerJob::Prepare ? "Preparing project assets..."
                                                                               : "Scanning Source content...";
    u.label(row, status, u.theme.text_dim);
  }
  else if (!picker.diagnostics.empty() || !warnings.empty()) {
    const int messages = int(picker.diagnostics.size() + warnings.size());
    Recti list = u.popup_row(rh * std::min(messages, 4));
    list.x += u.px(4);
    list.w -= u.px(8);
    const int offset = u.begin_scroll(u.id("source_diagnostics"), list, messages * rh);
    for (int i = std::max(0, offset / rh); i < messages; ++i) {
      const Recti row{list.x, list.y + i * rh - offset, list.w - u.px(12), rh};
      if (row.y >= list.bottom()) break;
      const std::string &message = (size_t)i < picker.diagnostics.size()
                                       ? picker.diagnostics[(size_t)i]
                                       : warnings[(size_t)i - picker.diagnostics.size()];
      u.label(row, message, u.theme.warning);
      u.tooltip(message);
    }
    u.end_scroll();
  }

  const Recti footer = u.popup_row(rh + u.px(10)).shrink(u.px(4));
  const Recti cancel{footer.right() - u.px(80), footer.y, u.px(80), footer.h};
  const Recti apply{cancel.x - u.px(150), footer.y, u.px(142), footer.h};
  const char *import = picker.ready && !picker.diagnostics.empty() ? "Import With Notes" : "Import";
  if (u.button_enabled(apply, import, idle && picker.selected >= 0 && !import_job_ && !ipv_.open,
                       picker.selected < 0 ? "Select a Source entry" : "An import is in progress")) {
    const auto kind = picker.ready ? SourcePickerJob::Prepare : SourcePickerJob::Read;
    source_picker_start(kind, picker.entries[(size_t)picker.selected].virtual_path);
  }
  if (u.button(cancel, "Cancel")) {
    source_picker_cancel();
    close();
  }
}

}  // namespace bl

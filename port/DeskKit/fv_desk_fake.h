// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_fake.h — a headless desktop: a scripted shell, a command runner
/// and a renderer, for testing a feature before any native shell shows it.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fv_desk_desk.h"
#include "fvkit/settings.h"

namespace fv {
class PixelBuffer;

namespace desk {

/// A `DeskShell` whose answers are fields and which records what it was asked.
class FakeDeskShell : public DeskShell {
 public:
  // What the user will answer.
  SaveAnswer next_save_answer = SaveAnswer::kDiscard;
  std::vector<std::string> files_to_open;  ///< empty: cancelled
  std::string save_spec;                   ///< empty: cancelled
  int save_format = 0;
  std::optional<int> list_choice;
  bool revert_answer = false;

  // What was asked or shown.
  std::vector<std::string> asked_save;
  app::FileTypeDesc last_open_chooser;  ///< what the last open dialog offered
  std::vector<Status> errors;
  int menus_changed = 0;
  int quit_calls = 0;
  int invalidate_calls = 0;
  std::string editor_mode;
  std::shared_ptr<OptionsModel> options_shown;  ///< the last options dialog

  SaveAnswer AskSave(const std::string& name) override;
  std::vector<std::string> ChooseFilesToOpen(const app::FileTypeDesc&) override;
  std::pair<std::string, int> ChooseSaveSpec(const app::FileTypeDesc&,
                                             const std::string& suggested) override;
  std::optional<int> ChooseFromList(const std::string&, const std::vector<std::string>&) override;
  bool ConfirmRevert(const std::string&) override { return revert_answer; }
  void SetCursor(app::CursorId) override {}
  void ShowHint(const app::HintText&) override {}
  void ShowContextMenu(PixelPoint, const app::MenuNode&) override {}
  void RequestInvalidate() override { ++invalidate_calls; }
  void OnEditorChanged(const app::TypeId& id, app::OverlayEditor*) override { editor_mode = id; }
  void ReportError(const Status& s) override { errors.push_back(s); }
  void MenusChanged() override { ++menus_changed; }
  void Quit() override { ++quit_calls; }
  void ShowOverlayOptions(std::shared_ptr<OptionsModel> model) override {
    options_shown = std::move(model);
  }
};

/// A Desk over a FakeDeskShell and its own Settings, with a fixed surface.
class FakeDesk {
 public:
  /// The view gets a `width` x `height` point surface at one pixel per point.
  explicit FakeDesk(int width = 800, int height = 600, MapGroups groups = MapGroups::Builtin());

  FakeDeskShell& shell() { return shell_; }
  Settings& settings() { return settings_; }
  Desk& desk() { return *desk_; }

  /// Executes each command id in order; stops at and returns the first error.
  Status Run(const std::vector<std::string>& command_ids);

  /// Draws the base map of the current product (raster formats only) and the
  /// overlay stack at the current view.
  Status Render(PixelBuffer* out);
  Status RenderPng(const std::string& path);

 private:
  FakeDeskShell shell_;
  Settings settings_;
  std::unique_ptr<Desk> desk_;
};

}  // namespace desk
}  // namespace fv

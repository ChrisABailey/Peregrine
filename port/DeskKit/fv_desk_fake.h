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
  /// Answers for successive lists, used before `list_choice` while any remain.
  std::vector<std::optional<int>> list_choices;
  bool revert_answer = false;
  std::string directory;                   ///< empty: cancelled
  bool properties_tabs = false;            ///< the shell hosts the Properties tabs

  // What was asked or shown.
  std::vector<std::string> asked_save;
  app::FileTypeDesc last_open_chooser;  ///< what the last open dialog offered
  std::vector<Status> errors;
  int menus_changed = 0;
  int quit_calls = 0;
  int invalidate_calls = 0;
  std::string editor_mode;
  std::shared_ptr<OptionsModel> options_shown;      ///< the last overlay options dialog
  std::shared_ptr<OptionsModel> map_options_shown;  ///< the last map options dialog
  std::vector<std::string> list_rows;           ///< what the last list offered
  std::vector<std::vector<std::string>> lists;  ///< every list offered, in order
  std::vector<std::string> notices;
  int data_sources_shown = 0;
  int route_profile_shown = 0;
  int layers_shown = 0;
  int layers_hidden = 0;
  int find_shown = 0;
  int feature_form_shown = 0;
  int item_info_shown = 0;
  int attribute_table_shown = 0;
  int layer_style_shown = 0;
  int layer_fields_shown = 0;
  int new_layer_shown = 0;
  int layer_properties_shown = 0;
  int select_by_attributes_shown = 0;
  int add_rule_shown = 0;
  int rule_rename_shown = 0;
  int rule_type_shown = 0;
  int rule_filter_shown = 0;

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
  void ShowMapOptions(std::shared_ptr<OptionsModel> model) override {
    map_options_shown = std::move(model);
  }
  void ShowDataSources() override { ++data_sources_shown; }
  void ShowRouteProfile() override { ++route_profile_shown; }
  void ShowLayers() override { ++layers_shown; }
  void HideLayers() override { ++layers_hidden; }
  void ShowFind() override { ++find_shown; }
  void ShowFeatureForm() override { ++feature_form_shown; }
  void ShowItemInfo() override { ++item_info_shown; }
  void ShowAttributeTable() override { ++attribute_table_shown; }
  void ShowLayerStyle() override { ++layer_style_shown; }
  void ShowLayerFields() override { ++layer_fields_shown; }
  void ShowNewLayer() override { ++new_layer_shown; }
  void ShowLayerProperties() override { ++layer_properties_shown; }
  bool HostsPropertiesTabs() const override { return properties_tabs; }
  void ShowSelectByAttributes() override { ++select_by_attributes_shown; }
  void ShowAddRule() override { ++add_rule_shown; }
  void BeginRuleRename() override { ++rule_rename_shown; }
  void ShowRuleType() override { ++rule_type_shown; }
  void ShowRuleFilter() override { ++rule_filter_shown; }
  std::string ChooseDirectory(const std::string&) override { return directory; }
  void ShowNotice(const std::string& text) override { notices.push_back(text); }
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

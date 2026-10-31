// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_rules.cpp — the Desk's style rule edits, the rule menu and the
/// rule filter builder and the rule style pages (port/layer-manager-plan.md §5).
#include "fv_desk_desk.h"

#include <fstream>
#include <sstream>

#include "fvkit/overlay/feature_overlay.h"

namespace fv {
namespace desk {
namespace {

/// The open and save dialogs' file type for a style.json.
app::FileTypeDesc StyleFileType() {
  app::FileTypeDesc d;
  d.default_extension = "json";
  d.open_filters = {{"MapLibre Style (*.json)", "*.json"}};
  d.save_filters = d.open_filters;
  return d;
}

/// Finds the feature layer behind `overlay` and the row of its rule named `id`.
Status FindRule(const OverlayManager& overlays, const Overlay* overlay, const std::string& id,
                std::shared_ptr<FeatureOverlay>* out, size_t* row) {
  *out = std::dynamic_pointer_cast<FeatureOverlay>(overlays.FindShared(overlay));
  if (!*out) return Status::Error(kNotFound, "the layer has closed");
  const std::vector<RuleRow> rules = StyleRulesModel::Rows(**out);
  *row = 0;
  while (*row < rules.size() && rules[*row].id != id) ++*row;
  if (*row == rules.size()) return Status::Error(kNotFound, "the rule \"" + id + "\" is gone");
  return Status::Ok();
}

}  // namespace

void Desk::RegisterRuleCommands() {
  auto add = [this](const char* id, const char* label, std::function<void()> action,
                    std::function<bool()> enabled) {
    Command c;
    c.id = id;
    c.label = label;
    c.action = std::move(action);
    c.enabled = std::move(enabled);
    const Status s = commands_.Register(std::move(c));
    if (!s.ok()) Warn(s.message);
  };
  auto report = [this](const Status& s) {
    if (!s.ok()) Report(s);
  };
  auto has_rule = [this] { return CurrentRule() >= 0; };
  auto has_layer = [this] {
    const auto* f = dynamic_cast<const FeatureOverlay*>(overlays_.current());
    return f != nullptr && f->layer() != nullptr;
  };
  add("layers.import_style", "Import Style…", [this, report] {
    const int row = CurrentLayerRow();
    if (row < 0) return;
    const std::vector<std::string> paths = shell_.ChooseFilesToOpen(StyleFileType());
    if (!paths.empty()) report(ImportStyle(static_cast<size_t>(row), paths.front()));
  }, has_layer);
  add("layers.export_style", "Export Style…", [this, report] {
    const int row = CurrentLayerRow();
    if (row < 0) return;
    const std::string suggested = layers_.Listed()[static_cast<size_t>(row)]->Name() + ".json";
    const std::pair<std::string, int> spec = shell_.ChooseSaveSpec(StyleFileType(), suggested);
    if (!spec.first.empty()) report(ExportStyle(static_cast<size_t>(row), spec.first));
  }, has_layer);
  add("layers.rule.add", "Add Rule…", [this] { shell_.ShowAddRule(); }, has_layer);
  add("layers.rule.edit_style", "Edit Style…", [this, report] { report(OpenRuleStyle()); }, has_rule);
  add("layers.rule.edit_filter", "Edit Filter…", [this, report] { report(OpenRuleFilter()); }, has_rule);
  add("layers.rule.duplicate", "Duplicate", [this, report] {
    if (CurrentRule() >= 0) report(DuplicateRule(CurrentLayerRow(), CurrentRule()));
  }, has_rule);
  add("layers.rule.rename", "Rename", [this] { shell_.BeginRuleRename(); }, has_rule);
  add("layers.rule.change_type", "Change Type…", [this] { shell_.ShowRuleType(); }, has_rule);
  add("layers.rule.add_casing", "Add Casing", [this, report] {
    if (CurrentRule() >= 0) report(AddRuleCasing(CurrentLayerRow(), CurrentRule()));
  }, [this] {
    const auto* f = dynamic_cast<const FeatureOverlay*>(rule_overlay_);
    const int row = CurrentRule();
    return row >= 0 && f != nullptr && StyleRulesModel::Rows(*f)[static_cast<size_t>(row)].type == "line";
  });
  add("layers.rule.delete", "Delete", [this, report] {
    if (CurrentRule() >= 0) report(DeleteRule(CurrentLayerRow(), CurrentRule()));
  }, has_rule);
}

Status Desk::ImportStyle(size_t layer_row, const std::string& path) {
  std::shared_ptr<FeatureOverlay> o;
  Status s = RuleLayer(layer_row, &o);
  if (!s.ok()) return s;
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::Error(kNotFound, "cannot open " + path);
  std::ostringstream text;
  text << in.rdbuf();
  FeatureStyle style;
  s = ImportFeatureStyleJson(text.str(), &style);
  if (!s.ok()) return Status::Error(kInvalidArg, path + ": " + s.message);
  // Another layer's overrides name its features, not these.
  style.overrides = o->style().overrides;
  return rules_.Replace(o, style, "Import Style");
}

Status Desk::ExportStyle(size_t layer_row, const std::string& path) const {
  std::shared_ptr<FeatureOverlay> o;
  Status s = RuleLayer(layer_row, &o);
  if (!s.ok()) return s;
  std::string text;
  s = ExportFeatureStyleJson(o->style(), &text);
  if (!s.ok()) return s;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!(out << text << '\n')) return Status::Error(kInternal, "cannot write " + path);
  return Status::Ok();
}

Status Desk::RuleLayer(size_t layer_row, std::shared_ptr<FeatureOverlay>* out) const {
  std::shared_ptr<Overlay> o;
  Status s = layers_.At(layer_row, &o);
  if (!s.ok()) return s;
  *out = std::dynamic_pointer_cast<FeatureOverlay>(o);
  if (!*out || !(*out)->layer()) return Status::Error(kNotFound, "\"" + o->Name() + "\" has no style rules");
  return Status::Ok();
}

int Desk::CurrentLayerRow() const {
  const std::vector<std::shared_ptr<Overlay>> listed = layers_.Listed();
  for (size_t i = 0; i < listed.size(); ++i)
    if (listed[i].get() == overlays_.current()) return static_cast<int>(i);
  return -1;
}

void Desk::RulesChanged(FeatureOverlay& overlay) {
  ++layers_generation_;
  ++feature_views_gen_;
  // An unedited style dialog follows the rules; one with edits keeps them.
  if (style_ && style_->overlay() == &overlay && !style_->dirty()) style_->Revert();
  // Unedited rule style pages follow their rule; a deleted or renamed rule closes them.
  if (rule_style_ && rule_style_->overlay() == &overlay && !rule_style_->dirty() && !rule_style_->Revert().ok())
    CloseRuleStyle();
  shell_.RequestInvalidate();
}

Status Desk::SelectRule(size_t layer_row, size_t rule_row) {
  std::shared_ptr<FeatureOverlay> o;
  Status s = RuleLayer(layer_row, &o);
  if (!s.ok()) return s;
  const std::vector<RuleRow> rules = StyleRulesModel::Rows(*o);
  if (rule_row >= rules.size()) return Status::Error(kInvalidArg, "no rule row " + std::to_string(rule_row));
  s = layers_.Select(layer_row);
  if (!s.ok()) return s;
  rule_overlay_ = o.get();
  rule_id_ = rules[rule_row].id;
  ++layers_generation_;
  return Status::Ok();
}

int Desk::CurrentRule() const {
  if (rule_id_.empty() || rule_overlay_ == nullptr || overlays_.current() != rule_overlay_) return -1;
  const auto* f = dynamic_cast<const FeatureOverlay*>(rule_overlay_);
  if (f == nullptr) return -1;
  const std::vector<RuleRow> rules = StyleRulesModel::Rows(*f);
  for (size_t i = 0; i < rules.size(); ++i)
    if (rules[i].id == rule_id_) return static_cast<int>(i);
  return -1;
}

Status Desk::AddRule(size_t layer_row, const std::string& name, const std::string& type) {
  std::shared_ptr<FeatureOverlay> o;
  Status s = RuleLayer(layer_row, &o);
  if (s.ok()) s = rules_.Add(o, name, type);
  if (s.ok()) s = SelectRule(layer_row, 0);
  return s;
}

Status Desk::DuplicateRule(size_t layer_row, size_t rule_row) {
  std::shared_ptr<FeatureOverlay> o;
  Status s = RuleLayer(layer_row, &o);
  std::string copy;
  if (s.ok()) s = rules_.Duplicate(o, rule_row, &copy);
  if (s.ok()) s = SelectRule(layer_row, rule_row);  // the copy sits directly above, at the original's row
  return s;
}

Status Desk::RenameRule(size_t layer_row, size_t rule_row, const std::string& name) {
  std::shared_ptr<FeatureOverlay> o;
  Status s = RuleLayer(layer_row, &o);
  if (!s.ok()) return s;
  const bool was_current = static_cast<int>(rule_row) == CurrentRule() && overlays_.current() == o.get();
  s = rules_.Rename(o, rule_row, name);
  if (s.ok() && was_current) rule_id_ = name;
  return s;
}

Status Desk::SetRuleType(size_t layer_row, size_t rule_row, const std::string& type) {
  std::shared_ptr<FeatureOverlay> o;
  Status s = RuleLayer(layer_row, &o);
  return s.ok() ? rules_.SetType(o, rule_row, type) : s;
}

Status Desk::SetRuleShown(size_t layer_row, size_t rule_row, bool shown) {
  std::shared_ptr<FeatureOverlay> o;
  Status s = RuleLayer(layer_row, &o);
  return s.ok() ? rules_.SetShown(o, rule_row, shown) : s;
}

Status Desk::MoveRule(size_t layer_row, size_t from, size_t to) {
  std::shared_ptr<FeatureOverlay> o;
  Status s = RuleLayer(layer_row, &o);
  return s.ok() ? rules_.Move(o, from, to) : s;
}

Status Desk::DeleteRule(size_t layer_row, size_t rule_row) {
  std::shared_ptr<FeatureOverlay> o;
  Status s = RuleLayer(layer_row, &o);
  return s.ok() ? rules_.Delete(o, rule_row) : s;
}

Status Desk::AddRuleCasing(size_t layer_row, size_t rule_row) {
  std::shared_ptr<FeatureOverlay> o;
  Status s = RuleLayer(layer_row, &o);
  if (s.ok()) s = rules_.AddCasing(o, rule_row);
  if (s.ok()) s = SelectRule(layer_row, rule_row + 1);  // the casing sits directly beneath
  return s;
}

Status Desk::OpenRuleFilter() {
  const int row = CurrentRule();
  if (row < 0) return Status::Error(kNotFound, "no rule is selected");
  const auto o = std::dynamic_pointer_cast<FeatureOverlay>(overlays_.FindShared(rule_overlay_));
  if (!o) return Status::Error(kNotFound, "the layer has closed");
  const RuleRow rule = StyleRulesModel::Rows(*o)[static_cast<size_t>(row)];
  std::string guard, rest;
  SplitRuleFilter(rule.filter, &guard, &rest);
  std::shared_ptr<QueryBuilderModel> q;
  Status s = QueryBuilderModel::Open(o, rest, &q);
  if (!s.ok()) return s;
  rule_filter_ = std::move(q);
  rule_filter_id_ = rule.id;
  rule_filter_guard_ = guard;
  ++feature_views_gen_;
  shell_.ShowRuleFilter();
  return Status::Ok();
}

Status Desk::ApplyRuleFilter() {
  if (!rule_filter_) return Status::Error(kNotFound, "no rule filter is open");
  std::shared_ptr<FeatureOverlay> o;
  size_t row = 0;
  Status s = FindRule(overlays_, rule_filter_->overlay(), rule_filter_id_, &o, &row);
  std::string expression;
  if (s.ok()) s = rule_filter_->Expression(&expression);
  if (s.ok()) s = rules_.SetFilter(o, row, JoinRuleFilter(rule_filter_guard_, expression));
  if (s.ok()) rule_filter_->MarkApplied();
  return s;
}

void Desk::CloseRuleFilter() {
  if (!rule_filter_) return;
  rule_filter_.reset();
  rule_filter_id_.clear();
  rule_filter_guard_.clear();
  ++feature_views_gen_;
}

Status Desk::OpenRuleStyle() {
  if (CurrentRule() < 0) return Status::Error(kNotFound, "no rule is selected");
  const auto o = std::dynamic_pointer_cast<FeatureOverlay>(overlays_.FindShared(rule_overlay_));
  if (!o) return Status::Error(kNotFound, "the layer has closed");
  if (!rule_style_ || rule_style_->overlay() != o.get() || rule_style_->rule_id() != rule_id_) {
    std::shared_ptr<RuleStyleModel> m;
    Status s = RuleStyleModel::Open(o, rule_id_, &m);
    if (!s.ok()) return s;
    rule_style_ = std::move(m);
    ++feature_views_gen_;
  }
  shell_.ShowRuleStyle();
  return Status::Ok();
}

Status Desk::ApplyRuleStyle() {
  if (!rule_style_) return Status::Error(kNotFound, "no rule style is open");
  std::shared_ptr<FeatureOverlay> o;
  size_t row = 0;
  Status s = FindRule(overlays_, rule_style_->overlay(), rule_style_->rule_id(), &o, &row);
  StyleRule draft;
  if (s.ok()) s = rule_style_->Draft(&draft);
  // Hold the model: the change callback may revert or close it.
  const std::shared_ptr<RuleStyleModel> m = rule_style_;
  if (s.ok()) s = rules_.SetStyle(o, row, draft);
  if (s.ok()) s = m->Revert();
  return s;
}

Status Desk::RevertRuleStyle() {
  if (!rule_style_) return Status::Error(kNotFound, "no rule style is open");
  Status s = rule_style_->Revert();
  ++feature_views_gen_;
  return s;
}

void Desk::CloseRuleStyle() {
  if (!rule_style_) return;
  rule_style_.reset();
  ++feature_views_gen_;
}

}  // namespace desk
}  // namespace fv

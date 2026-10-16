// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fv_desk_fake.h"
#include "fv_desk_style_rules.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/overlay/feature_overlay.h"
#include "fvkit/raster.h"
#include "scratch.h"

namespace {

using fv::FeatureOverlay;
using fv::FeatureRenderer;
using fv::GeoPoint;
using fv::desk::FakeDesk;
using fv::desk::MenuItem;
using fv::desk::RuleRow;

const std::string kRivers = FV_FEATURE_RIVERS;
const std::string kGrid = std::string("overlay.toggle.") + fv::app::kGridTypeId;

std::vector<uint8_t> Pixels(FakeDesk& fd) {
  fv::PixelBuffer buf;
  EXPECT_TRUE(fd.Render(&buf).ok());
  std::vector<uint8_t> out;
  for (int y = 0; y < buf.Height(); ++y) out.insert(out.end(), buf.Row(y), buf.Row(y) + 4 * buf.Width());
  return out;
}

std::vector<std::string> Ids(const std::vector<RuleRow>& rows) {
  std::vector<std::string> ids;
  for (const RuleRow& r : rows) ids.push_back(r.id);
  return ids;
}

TEST(RuleFilter, SplitsTheGeometryTestFromTheRestAndJoinsThemBack) {
  const std::string geom = R"(["match",["geometry-type"],["Point","MultiPoint"],true,false])";
  const std::string deep = R"([">",["get","depth"],7])";
  const std::string named = R"(["has","name"])";
  std::string guard, rest;
  fv::desk::SplitRuleFilter(geom, &guard, &rest);
  EXPECT_EQ(guard, geom);
  EXPECT_EQ(rest, "");
  fv::desk::SplitRuleFilter(deep, &guard, &rest);
  EXPECT_EQ(guard, "");
  EXPECT_EQ(rest, deep);
  fv::desk::SplitRuleFilter("", &guard, &rest);
  EXPECT_EQ(guard + rest, "");

  const std::string joined = fv::desk::JoinRuleFilter(geom, deep);
  EXPECT_EQ(joined, R"(["all",)" + geom + "," + deep + "]");
  fv::desk::SplitRuleFilter(joined, &guard, &rest);
  EXPECT_EQ(guard, geom);
  EXPECT_EQ(rest, deep);

  // Several terms on either side stay together in an `all`.
  const std::string both = R"(["all",)" + geom + "," + deep + "," + named + "]";
  fv::desk::SplitRuleFilter(both, &guard, &rest);
  EXPECT_EQ(guard, geom);
  EXPECT_EQ(rest, R"(["all",)" + deep + "," + named + "]");
  EXPECT_EQ(fv::desk::JoinRuleFilter(geom, ""), geom);
  EXPECT_EQ(fv::desk::JoinRuleFilter("", deep), deep);
}

/// A FakeDesk on Charleston harbour with the grid and a copy of the rivers
/// sample (three lines, fields name and depth) open.
class StyleRulesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto& mv = fd_.desk().map_view();
    mv.SetViewport(mv.View().WithCamera(GeoPoint{32.86, -79.95}, 250000, 0));
    path_ = dir_.Path("rivers.geojson");
    std::filesystem::copy_file(kRivers, path_);
    ASSERT_TRUE(fd_.Run({kGrid}).ok());
    fd_.shell().files_to_open = {path_};
    ASSERT_TRUE(fd_.Run({"overlay.open"}).ok());
    ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
    rivers_ = std::dynamic_pointer_cast<FeatureOverlay>(fd_.desk().overlays().OfType(FeatureOverlay::kTypeId)[0]);
    ASSERT_TRUE(rivers_);
  }

  fv::desk::Desk& desk() { return fd_.desk(); }
  size_t Row() {
    const auto rows = fd_.desk().layers().Rows();
    for (size_t i = 0; i < rows.size(); ++i)
      if (rows[i].overlay == rivers_.get()) return i;
    ADD_FAILURE() << "rivers not listed";
    return 0;
  }
  std::vector<RuleRow> Rules() { return fd_.desk().layers().Rows()[Row()].rules; }
  size_t RuleRowOf(const std::string& id) {
    const std::vector<std::string> ids = Ids(Rules());
    const auto it = std::find(ids.begin(), ids.end(), id);
    EXPECT_NE(it, ids.end()) << id;
    return static_cast<size_t>(it - ids.begin());
  }
  bool Enabled(const std::vector<MenuItem>& menu, const std::string& id) {
    for (const MenuItem& m : menu)
      if (m.command_id == id) return fd_.desk().commands().IsEnabled(id);
    ADD_FAILURE() << id << " not in the menu";
    return false;
  }

  fv::desk::test::Scratch dir_;
  FakeDesk fd_;
  std::string path_;
  std::shared_ptr<FeatureOverlay> rivers_;
};

TEST_F(StyleRulesTest, BothViewsListTheRulesTopDrawnFirst) {
  fv::FeatureStyle generated;
  ASSERT_TRUE(fv::FeatureStyleToRules(rivers_->style(), &generated, fv::GeometryType::kLineString).ok());
  std::vector<std::string> top_first;
  for (auto it = generated.rules.rbegin(); it != generated.rules.rend(); ++it) top_first.push_back(it->id);
  // A line layer lists only the rules a line draws.
  EXPECT_EQ(top_first, (std::vector<std::string>{"Lines", "Line casings"}));
  EXPECT_EQ(Ids(Rules()), top_first);
  // A point layer's generated style lists its marker and label rules; any geometry lists every rule.
  fv::FeatureStyle points, any;
  ASSERT_TRUE(fv::FeatureStyleToRules(rivers_->style(), &points, fv::GeometryType::kPoint).ok());
  ASSERT_TRUE(fv::FeatureStyleToRules(rivers_->style(), &any).ok());
  ASSERT_EQ(points.rules.size(), 1u);
  EXPECT_EQ(points.rules[0].id, "Points");
  EXPECT_GT(any.rules.size(), top_first.size());
  // Listing a generated style does not turn it into rules.
  EXPECT_EQ(rivers_->style().renderer, FeatureRenderer::kSingle);

  bool found = false;
  for (const fv::desk::SourceRow& s : desk().sources().Rows())
    for (size_t layer : s.layers)
      if (desk().layers().Rows()[layer].overlay == rivers_.get()) {
        EXPECT_EQ(Ids(desk().layers().Rows()[layer].rules), top_first);
        found = true;
      }
  EXPECT_TRUE(found);

  // A layer without features has no rules.
  for (const fv::desk::LayerRow& r : desk().layers().Rows())
    if (r.overlay != rivers_.get()) EXPECT_TRUE(r.rules.empty()) << r.name;
}

TEST_F(StyleRulesTest, DuplicateRenameFilterAndMoveUnderTheOriginalEachUndo) {
  const std::vector<std::string> generated = Ids(Rules());
  const std::vector<uint8_t> before = Pixels(fd_);
  const size_t lines = RuleRowOf("Lines");
  const std::string guard = Rules()[lines].filter;  // the generator's geometry-type test
  const uint64_t gen = desk().LayersGeneration();

  // Duplicate: the copy lands directly above the original and becomes current.
  ASSERT_TRUE(desk().SelectRule(Row(), lines).ok());
  EXPECT_EQ(desk().CurrentRule(), static_cast<int>(lines));
  ASSERT_TRUE(fd_.Run({"layers.rule.duplicate"}).ok());
  ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  EXPECT_EQ(rivers_->style().renderer, FeatureRenderer::kRules);
  EXPECT_TRUE(rivers_->AsPersistence()->is_dirty());
  EXPECT_NE(desk().LayersGeneration(), gen);
  ASSERT_EQ(Rules().size(), generated.size() + 1);
  EXPECT_EQ(Rules()[lines].id, "Lines copy");
  EXPECT_EQ(Rules()[lines + 1].id, "Lines");
  EXPECT_EQ(desk().CurrentRule(), static_cast<int>(lines));
  EXPECT_EQ(desk().undo().UndoLabel(), "Duplicate Rule");
  // An identical copy drawn over the original changes no pixel.
  EXPECT_EQ(Pixels(fd_), before);

  // Rename keeps it current.
  ASSERT_TRUE(desk().RenameRule(Row(), lines, "Cooper").ok());
  EXPECT_EQ(Rules()[lines].id, "Cooper");
  EXPECT_EQ(desk().CurrentRule(), static_cast<int>(lines));
  EXPECT_EQ(desk().RenameRule(Row(), lines, "Lines").code, fv::kInvalidArg);
  EXPECT_EQ(desk().RenameRule(Row(), lines, "").code, fv::kInvalidArg);

  // Filter through the query builder.
  ASSERT_TRUE(fd_.Run({"layers.rule.edit_filter"}).ok());
  ASSERT_TRUE(desk().rule_filter());
  EXPECT_EQ(desk().rule_filter_rule(), "Cooper");
  // The builder opens empty; the geometry test is kept out of it and put back.
  EXPECT_FALSE(desk().rule_filter()->custom());
  EXPECT_TRUE(desk().rule_filter()->query().clauses.empty());
  const size_t c = desk().rule_filter()->AddClause();
  ASSERT_TRUE(desk().rule_filter()->SetClause(c, {"name", fv::desk::QueryOp::kEqual, "Cooper River"}).ok());
  ASSERT_TRUE(desk().ApplyRuleFilter().ok());
  const std::string cooper = R"(["all",)" + guard + R"(,["==",["get","name"],"Cooper River"]])";
  EXPECT_EQ(Rules()[lines].filter, cooper);
  EXPECT_FALSE(desk().rule_filter()->dirty());

  // Move the copy under the original.
  ASSERT_TRUE(desk().MoveRule(Row(), lines, lines + 1).ok());
  EXPECT_EQ(Rules()[lines].id, "Lines");
  EXPECT_EQ(Rules()[lines + 1].id, "Cooper");
  EXPECT_EQ(desk().CurrentRule(), static_cast<int>(lines + 1));

  // Undo each step.
  EXPECT_EQ(desk().undo().UndoLabel(), "Move Rule");
  ASSERT_TRUE(desk().undo().Undo().ok());
  EXPECT_EQ(Rules()[lines].id, "Cooper");
  EXPECT_EQ(desk().undo().UndoLabel(), "Edit Rule Filter");
  ASSERT_TRUE(desk().undo().Undo().ok());
  EXPECT_EQ(Rules()[lines].filter, guard);
  EXPECT_EQ(desk().undo().UndoLabel(), "Rename Rule");
  ASSERT_TRUE(desk().undo().Undo().ok());
  EXPECT_EQ(Rules()[lines].id, "Lines copy");
  EXPECT_EQ(desk().undo().UndoLabel(), "Duplicate Rule");
  ASSERT_TRUE(desk().undo().Undo().ok());
  EXPECT_EQ(Ids(Rules()), generated);
  EXPECT_EQ(rivers_->style().renderer, FeatureRenderer::kSingle);
  EXPECT_EQ(Pixels(fd_), before);

  // And redo them all.
  for (int i = 0; i < 4; ++i) ASSERT_TRUE(desk().undo().Redo().ok());
  EXPECT_EQ(Rules()[lines].id, "Lines");
  EXPECT_EQ(Rules()[lines + 1].id, "Cooper");
  EXPECT_EQ(Rules()[lines + 1].filter, cooper);
}

TEST_F(StyleRulesTest, HidingARuleHidesItsInkAndItsHits) {
  const std::vector<uint8_t> before = Pixels(fd_);
  const size_t lines = RuleRowOf("Lines");
  ASSERT_TRUE(desk().SetRuleShown(Row(), lines, false).ok());
  EXPECT_FALSE(Rules()[lines].shown);
  EXPECT_NE(Pixels(fd_), before);
  // Showing a rule that is already shown is no edit.
  const size_t undos = desk().undo().undo_count();
  ASSERT_TRUE(desk().SetRuleShown(Row(), RuleRowOf("Line casings"), true).ok());
  EXPECT_EQ(desk().undo().undo_count(), undos);

  // With every rule hidden no feature is drawn, so none can be hit (LM-D8).
  const size_t n = Rules().size();
  for (size_t i = 0; i < n; ++i) ASSERT_TRUE(desk().SetRuleShown(Row(), i, false).ok());
  const fv::GeoRect world{{-90, -180}, {90, 180}};
  std::vector<fv::Feature> drawn;
  ASSERT_TRUE(rivers_->QueryDrawn(world, 250000, &drawn).ok());
  EXPECT_TRUE(drawn.empty());
  for (size_t i = 0; i < n; ++i) ASSERT_TRUE(desk().undo().Undo().ok());
  EXPECT_TRUE(Rules()[lines].shown);
  ASSERT_TRUE(rivers_->QueryDrawn(world, 250000, &drawn).ok());
  EXPECT_EQ(drawn.size(), 3u);
  EXPECT_EQ(Pixels(fd_), before);
}

TEST_F(StyleRulesTest, AddChangeTypeAndDelete) {
  const size_t n = Rules().size();
  EXPECT_EQ(desk().AddRule(Row(), "", "line").code, fv::kInvalidArg);
  EXPECT_EQ(desk().AddRule(Row(), "Lines", "line").code, fv::kInvalidArg);
  EXPECT_EQ(desk().AddRule(Row(), "Heat", "heatmap").code, fv::kInvalidArg);
  EXPECT_EQ(Rules().size(), n);
  EXPECT_EQ(rivers_->style().renderer, FeatureRenderer::kSingle);

  ASSERT_TRUE(desk().AddRule(Row(), "Deep", "line").ok());
  ASSERT_EQ(Rules().size(), n + 1);
  EXPECT_EQ(Rules()[0].id, "Deep");
  EXPECT_EQ(Rules()[0].type, "line");
  EXPECT_EQ(desk().CurrentRule(), 0);

  // A filter typed by hand; a new rule has no geometry test to keep.
  EXPECT_EQ(desk().rule_filter(), nullptr);
  ASSERT_TRUE(fd_.Run({"layers.rule.edit_filter"}).ok());
  ASSERT_TRUE(desk().rule_filter()->SetExpression(R"([">",["get","depth"],7])").ok());
  ASSERT_TRUE(desk().ApplyRuleFilter().ok());
  const std::string deep = Rules()[0].filter;
  EXPECT_EQ(deep, R"([">",["get","depth"],7.0])");

  // A filter the engine cannot evaluate is refused and changes nothing.
  fv::app::UndoStack undo;
  fv::desk::StyleRulesModel model(undo, nullptr);
  EXPECT_EQ(model.SetFilter(rivers_, 0, R"(["frobnicate",1])").code, fv::kInvalidArg);
  EXPECT_EQ(Rules()[0].filter, deep);
  EXPECT_FALSE(undo.CanUndo());

  // Change type carries the colour across; the filter stays.
  ASSERT_TRUE(desk().SetRuleType(Row(), 0, "circle").ok());
  EXPECT_EQ(Rules()[0].type, "circle");
  EXPECT_EQ(Rules()[0].filter, deep);
  const fv::StyleRule& r = rivers_->style().rules.back();
  EXPECT_NE(r.paint.find(R"("circle-color":"#1e5ac8")"), std::string::npos) << r.paint;
  EXPECT_EQ(desk().SetRuleType(Row(), 0, "raster").code, fv::kInvalidArg);

  ASSERT_TRUE(fd_.Run({"layers.rule.delete"}).ok());
  EXPECT_EQ(Rules().size(), n);
  EXPECT_EQ(desk().CurrentRule(), -1);
  EXPECT_EQ(desk().undo().UndoLabel(), "Delete Rule");
}

TEST_F(StyleRulesTest, MenusFollowTheCurrentRule) {
  ASSERT_TRUE(desk().SelectLayer(Row()).ok());
  EXPECT_TRUE(Enabled(desk().LayerMenu(), "layers.rule.add"));
  EXPECT_FALSE(Enabled(desk().RuleMenu(), "layers.rule.duplicate"));
  ASSERT_TRUE(fd_.Run({"layers.rule.add"}).ok());
  EXPECT_EQ(fd_.shell().add_rule_shown, 1);

  ASSERT_TRUE(desk().SelectRule(Row(), 0).ok());
  std::vector<std::string> ids;
  for (const MenuItem& m : desk().RuleMenu())
    if (!m.command_id.empty()) {
      ids.push_back(m.command_id);
      // Add Casing is for line rules only.
      EXPECT_EQ(desk().commands().IsEnabled(m.command_id),
                m.command_id != "layers.rule.add_casing" || Rules()[0].type == "line")
          << m.command_id;
    }
  EXPECT_EQ(ids, (std::vector<std::string>{"layers.rule.edit_style", "layers.rule.edit_filter",
                                           "layers.rule.duplicate", "layers.rule.rename",
                                           "layers.rule.change_type", "layers.rule.add_casing",
                                           "layers.rule.delete"}));
  ASSERT_TRUE(fd_.Run({"layers.rule.rename", "layers.rule.change_type"}).ok());
  EXPECT_EQ(fd_.shell().rule_rename_shown, 1);
  EXPECT_EQ(fd_.shell().rule_type_shown, 1);

  // Clicking a layer row drops the current rule; so does selecting another layer.
  ASSERT_TRUE(desk().SelectLayer(Row()).ok());
  EXPECT_EQ(desk().CurrentRule(), -1);
  ASSERT_TRUE(desk().SelectRule(Row(), 0).ok());
  for (size_t i = 0; i < desk().layers().Rows().size(); ++i)
    if (i != Row()) ASSERT_TRUE(desk().layers().Rows()[i].rules.empty());
  EXPECT_EQ(desk().SelectRule(Row() == 0 ? 1 : 0, 0).code, fv::kNotFound);
}

TEST_F(StyleRulesTest, RulesAreSavedInTheFile) {
  ASSERT_TRUE(desk().DuplicateRule(Row(), RuleRowOf("Lines")).ok());
  ASSERT_TRUE(desk().RenameRule(Row(), RuleRowOf("Lines copy"), "Cooper").ok());
  ASSERT_TRUE(desk().SetRuleShown(Row(), RuleRowOf("Cooper"), false).ok());
  const std::vector<RuleRow> edited = Rules();
  ASSERT_TRUE(fd_.Run({"file.save"}).ok());
  ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;

  FeatureOverlay reopened;
  ASSERT_TRUE(reopened.FileOpen(path_).ok());
  EXPECT_EQ(reopened.style().renderer, FeatureRenderer::kRules);
  const std::vector<RuleRow> read = fv::desk::StyleRulesModel::Rows(reopened);
  EXPECT_EQ(Ids(read), Ids(edited));
  EXPECT_FALSE(read[RuleRowOf("Cooper")].shown);
}

TEST_F(StyleRulesTest, ClosingTheLayerDropsItsRuleHistoryAndFilter) {
  ASSERT_TRUE(desk().SelectRule(Row(), RuleRowOf("Lines")).ok());
  ASSERT_TRUE(fd_.Run({"layers.rule.edit_filter"}).ok());
  ASSERT_TRUE(desk().rule_filter());
  ASSERT_TRUE(desk().SetRuleShown(Row(), RuleRowOf("Lines"), false).ok());
  EXPECT_TRUE(desk().undo().CanUndo());
  fd_.shell().next_save_answer = fv::app::AppShell::SaveAnswer::kDiscard;
  ASSERT_TRUE(fd_.Run({"layers.close"}).ok());
  EXPECT_FALSE(desk().rule_filter());
  EXPECT_FALSE(desk().undo().CanUndo());
}

}  // namespace

namespace {

/// The rows of the open style dialog's legend, as titles.
std::vector<std::string> LegendTitles(const fv::desk::LayerStyleModel& m) {
  std::vector<std::string> out;
  for (size_t i = 0; i < m.EntryCount(); ++i) out.push_back(m.EntryLabel(i));
  return out;
}

}  // namespace

TEST_F(StyleRulesTest, CategoriseIntoRulesDrawsTheSameAndRegenerateReplacesTheRules) {
  ASSERT_TRUE(desk().SelectLayer(Row()).ok());
  ASSERT_TRUE(desk().OpenLayerStyle().ok());
  const std::shared_ptr<fv::desk::LayerStyleModel> m = desk().layer_style();
  ASSERT_TRUE(m);
  EXPECT_EQ(m->CategoriseIntoRules().code, fv::kInvalidArg);  // single
  ASSERT_TRUE(m->SetRenderer(FeatureRenderer::kCategorised).ok());
  ASSERT_TRUE(m->SetField("name").ok());
  const size_t values = m->style().categories.size();
  ASSERT_GE(values, 2u);
  const fv::FeatureCategory first = m->style().categories[0];
  ASSERT_TRUE(desk().ApplyLayerStyle().ok());
  const std::vector<uint8_t> categorised = Pixels(fd_);

  ASSERT_TRUE(m->CategoriseIntoRules().ok());
  EXPECT_EQ(m->renderer(), FeatureRenderer::kRules);
  // Line colour varies by name; the casing does not, so it stays one rule.
  EXPECT_EQ(m->style().rules.size(), 1 + values + 1);
  EXPECT_EQ(m->style().rules[0].id, "Line casings");
  EXPECT_EQ(m->style().rules[1].id, "Lines: Other");
  // The legend: one row per value, top first, then Other, then the casing.
  const std::vector<std::string> legend = LegendTitles(*m);
  ASSERT_EQ(legend.size(), values + 2);
  EXPECT_EQ(legend[0], first.label.empty() ? first.value : first.label);
  EXPECT_EQ(legend[values], "Other");
  EXPECT_EQ(legend[values + 1], "Line casings");
  fv::PixelBuffer swatch;
  ASSERT_TRUE(m->RenderSwatch(0, 40, 20, 1.0, &swatch).ok());
  int inked = 0;
  for (int y = 0; y < 20; ++y)
    for (int x = 0; x < 40 * 4; ++x) inked += swatch.Row(y)[x] != 255;
  EXPECT_GT(inked, 20);
  // A legend row's label is its rules' legend title; it has no symbol or value to edit.
  ASSERT_TRUE(m->SetEntryLabel(values + 1, "Banks").ok());
  EXPECT_EQ(m->style().rules[0].legend, "Banks");
  EXPECT_EQ(m->SetEntryValue(0, "x").code, fv::kInvalidArg);
  EXPECT_EQ(m->AddEntry().code, fv::kInvalidArg);
  EXPECT_FALSE(m->SetEntrySymbol(0, fv::FeatureSymbol()).ok());

  ASSERT_TRUE(desk().ApplyLayerStyle().ok());
  EXPECT_EQ(rivers_->style().renderer, FeatureRenderer::kRules);
  EXPECT_EQ(Rules().size(), values + 2);
  EXPECT_EQ(Pixels(fd_), categorised);

  // Regenerate…: a generator replaces the rules.
  ASSERT_TRUE(m->SetRenderer(FeatureRenderer::kSingle).ok());
  ASSERT_TRUE(desk().ApplyLayerStyle().ok());
  EXPECT_EQ(rivers_->style().renderer, FeatureRenderer::kSingle);
  EXPECT_EQ(Ids(Rules()), (std::vector<std::string>{"Lines", "Line casings"}));
  // And a generated style turns into the rules that draw it.
  ASSERT_TRUE(m->SetRenderer(FeatureRenderer::kRules).ok());
  EXPECT_EQ(LegendTitles(*m), (std::vector<std::string>{"Lines", "Line casings"}));
}

TEST_F(StyleRulesTest, AnExportedStyleImportsIntoASecondLayerUnchanged) {
  ASSERT_TRUE(desk().SelectLayer(Row()).ok());
  EXPECT_TRUE(Enabled(desk().LayerMenu(), "layers.import_style"));
  EXPECT_TRUE(Enabled(desk().LayerMenu(), "layers.export_style"));
  ASSERT_TRUE(desk().DuplicateRule(Row(), RuleRowOf("Lines")).ok());
  fv::desk::StyleRulesModel rules(desk().undo(), nullptr);
  ASSERT_TRUE(rules.SetFilter(rivers_, RuleRowOf("Lines copy"), R"([">",["get","depth"],7])").ok());
  const fv::FeatureStyle styled = rivers_->style();

  const std::string exported = dir_.Path("rivers.style.json");
  fd_.shell().save_spec = exported;
  ASSERT_TRUE(fd_.Run({"layers.export_style"}).ok());
  ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  ASSERT_TRUE(std::filesystem::exists(exported));

  // A second copy of the rivers takes the style through Import Style….
  const std::string second = dir_.Path("rivers2.geojson");
  std::filesystem::copy_file(kRivers, second);
  fd_.shell().files_to_open = {second};
  ASSERT_TRUE(fd_.Run({"overlay.open"}).ok());
  std::shared_ptr<FeatureOverlay> other;
  for (const auto& o : desk().overlays().OfType(FeatureOverlay::kTypeId))
    if (o != rivers_) other = std::dynamic_pointer_cast<FeatureOverlay>(o);
  ASSERT_TRUE(other);
  size_t row = 0;
  while (desk().layers().Rows()[row].overlay != other.get()) ++row;
  ASSERT_TRUE(desk().SelectLayer(row).ok());
  fd_.shell().files_to_open = {exported};
  ASSERT_TRUE(fd_.Run({"layers.import_style"}).ok());
  ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  EXPECT_TRUE(other->style() == styled);
  EXPECT_TRUE(other->AsPersistence()->is_dirty());
  EXPECT_EQ(desk().undo().UndoLabel(), "Import Style");
  ASSERT_TRUE(desk().undo().Undo().ok());
  EXPECT_EQ(other->style().renderer, FeatureRenderer::kSingle);

  // A file that is not a style is reported and changes nothing.
  fd_.shell().files_to_open = {second};
  ASSERT_TRUE(fd_.Run({"layers.import_style"}).ok());
  ASSERT_EQ(fd_.shell().errors.size(), 1u);
  EXPECT_EQ(other->style().renderer, FeatureRenderer::kSingle);
}

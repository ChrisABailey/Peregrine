// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fv_desk_fake.h"
#include "fv_desk_layer_properties.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/overlay/feature_overlay.h"
#include "fvkit/raster.h"
#include "scratch.h"

namespace {

using fv::FeatureOverlay;
using fv::GeoPoint;
using fv::desk::FakeDesk;
using fv::desk::LayerGeneral;
using fv::desk::LayerGeneralModel;
using fv::desk::PropertiesTab;

const std::string kRivers = FV_FEATURE_RIVERS;
const std::string kGrid = std::string("overlay.toggle.") + fv::app::kGridTypeId;

TEST(ParseScale, ReadsWhatFormatScaleWritesAndPlainNumbers) {
  double d = -1;
  for (const char* text : {"1:50,000", "1:50000", "50,000", " 50000 ", "1: 50,000"}) {
    ASSERT_TRUE(fv::desk::ParseScale(text, &d).ok()) << text;
    EXPECT_EQ(d, 50000) << text;
  }
  ASSERT_TRUE(fv::desk::ParseScale(fv::desk::FormatScale(1250000), &d).ok());
  EXPECT_EQ(d, 1250000);
  ASSERT_TRUE(fv::desk::ParseScale("", &d).ok());
  EXPECT_EQ(d, 0);
  for (const char* text : {"big", "1:", "0", "-5", "1:2:3", "1:0.5"})
    EXPECT_EQ(fv::desk::ParseScale(text, &d).code, fv::kInvalidArg) << text;
}

TEST(LayerGeneralModel, DraftsAppliesAndReverts) {
  auto overlay = std::make_shared<fv::Overlay>("Grid");
  std::shared_ptr<LayerGeneralModel> m;
  ASSERT_TRUE(LayerGeneralModel::Open(overlay, &m).ok());
  EXPECT_EQ(m->general().name, "Grid");
  EXPECT_FALSE(m->dirty());

  LayerGeneral g = m->general();
  g.name = "";
  EXPECT_EQ(m->Set(g).code, fv::kInvalidArg);
  g.name = "Lat/long grid";
  g.min_scale = 50000;
  g.max_scale = 100000;  // zoomed in further than zoomed out: never drawn
  EXPECT_EQ(m->Set(g).code, fv::kInvalidArg);
  EXPECT_FALSE(m->dirty());
  g.min_scale = 1000000;
  g.description = "Every degree";
  g.credits = "FalconView";
  ASSERT_TRUE(m->Set(g).ok());
  EXPECT_TRUE(m->dirty());
  EXPECT_EQ(overlay->Name(), "Grid");

  ASSERT_TRUE(m->Apply().ok());
  EXPECT_FALSE(m->dirty());
  EXPECT_EQ(overlay->Name(), "Lat/long grid");
  EXPECT_EQ(overlay->description(), "Every degree");
  EXPECT_EQ(overlay->credits(), "FalconView");
  EXPECT_EQ(overlay->min_scale(), 1000000);
  EXPECT_EQ(overlay->max_scale(), 100000);

  g.credits = "";
  ASSERT_TRUE(m->Set(g).ok());
  ASSERT_TRUE(m->Revert().ok());
  EXPECT_EQ(m->general().credits, "FalconView");

  overlay.reset();
  EXPECT_EQ(m->Apply().code, fv::kNotFound);
}

/// Pixels of a rendered frame, for comparing two renders.
std::vector<uint8_t> Pixels(FakeDesk& fd) {
  fv::PixelBuffer buf;
  EXPECT_TRUE(fd.Render(&buf).ok());
  std::vector<uint8_t> out;
  for (int y = 0; y < buf.Height(); ++y) out.insert(out.end(), buf.Row(y), buf.Row(y) + 4 * buf.Width());
  return out;
}

/// A FakeDesk on Charleston harbour with the grid and a copy of the rivers
/// sample open, in a shell that hosts the Properties tabs.
class LayerPropertiesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fd_.shell().properties_tabs = true;
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
    ASSERT_EQ(fd_.desk().overlays().current(), rivers_.get());
  }

  fv::desk::Desk& desk() { return fd_.desk(); }

  fv::desk::test::Scratch dir_;
  FakeDesk fd_;
  std::string path_;
  std::shared_ptr<FeatureOverlay> rivers_;
};

TEST_F(LayerPropertiesTest, AFeatureLayerShowsEveryTab) {
  ASSERT_TRUE(fd_.Run({"layers.properties"}).ok());
  EXPECT_EQ(fd_.shell().layer_properties_shown, 1);
  EXPECT_EQ(desk().PropertiesTabs(),
            (std::vector<PropertiesTab>{PropertiesTab::kSource, PropertiesTab::kGeneral, PropertiesTab::kDisplay,
                                        PropertiesTab::kSymbology, PropertiesTab::kLabels, PropertiesTab::kFields,
                                        PropertiesTab::kSelection, PropertiesTab::kDefinitionQuery}));
  EXPECT_EQ(desk().properties_tab(), PropertiesTab::kGeneral);
  ASSERT_TRUE(desk().layer_general());
  EXPECT_EQ(desk().layer_general()->general().name, rivers_->Name());
  ASSERT_TRUE(desk().layer_style());
  EXPECT_EQ(desk().layer_style()->feature(), 0);
  ASSERT_TRUE(desk().layer_fields());
  EXPECT_FALSE(desk().LayerPropertiesDirty());
}

TEST_F(LayerPropertiesTest, AStaticOverlayOpensItsOverlayOptionsPage) {
  const auto grid = desk().overlays().FirstOfType(fv::app::kGridTypeId);
  ASSERT_TRUE(grid);
  ASSERT_TRUE(desk().overlays().MakeCurrent(grid).ok());
  ASSERT_TRUE(fd_.Run({"layers.properties"}).ok());
  EXPECT_EQ(fd_.shell().layer_properties_shown, 0);
  const auto& options = fd_.shell().options_shown;
  ASSERT_TRUE(options);
  ASSERT_GE(options->initial_page(), 0);
  EXPECT_EQ(options->pages()[static_cast<size_t>(options->initial_page())]->id(), fv::app::kGridTypeId);

  // Overlay ▸ Options states no page.
  ASSERT_TRUE(fd_.Run({"overlay.options"}).ok());
  EXPECT_EQ(fd_.shell().options_shown->initial_page(), -1);
}

TEST_F(LayerPropertiesTest, LayerStyleAndLayerFieldsOpenPropertiesAtTheirTab) {
  ASSERT_TRUE(fd_.Run({"overlay.style"}).ok());
  EXPECT_EQ(desk().properties_tab(), PropertiesTab::kSymbology);
  ASSERT_TRUE(fd_.Run({"overlay.fields"}).ok());
  EXPECT_EQ(desk().properties_tab(), PropertiesTab::kFields);
  EXPECT_EQ(fd_.shell().layer_properties_shown, 2);
  EXPECT_EQ(fd_.shell().layer_style_shown, 0);
  EXPECT_EQ(fd_.shell().layer_fields_shown, 0);
}

TEST_F(LayerPropertiesTest, AShellWithoutTabsKeepsItsOwnDialogs) {
  fd_.shell().properties_tabs = false;
  ASSERT_TRUE(fd_.Run({"overlay.style", "overlay.fields", "layers.properties"}).ok());
  EXPECT_EQ(fd_.shell().layer_style_shown, 1);
  EXPECT_EQ(fd_.shell().layer_fields_shown, 1);
  EXPECT_EQ(fd_.shell().layer_properties_shown, 1);
  EXPECT_EQ(desk().PropertiesTabs(), (std::vector<PropertiesTab>{PropertiesTab::kSource}));
  EXPECT_FALSE(desk().layer_general());
}

TEST_F(LayerPropertiesTest, ASourcesPropertiesIsTheSourceTabAlone) {
  ASSERT_TRUE(desk().SelectLayer(1).ok());  // the grid, below the rivers
  ASSERT_NE(desk().overlays().current(), rivers_.get());
  ASSERT_TRUE(fd_.Run({"layers.source.properties"}).ok());
  EXPECT_EQ(desk().PropertiesTabs(), (std::vector<PropertiesTab>{PropertiesTab::kSource}));
  EXPECT_EQ(desk().properties_tab(), PropertiesTab::kSource);
}

TEST_F(LayerPropertiesTest, ALayerIsHiddenOutsideItsScaleRange) {
  ASSERT_TRUE(fd_.Run({kGrid}).ok());  // the rivers alone
  rivers_->SetVisible(false);
  const std::vector<uint8_t> empty = Pixels(fd_);
  rivers_->SetVisible(true);
  const std::vector<uint8_t> drawn = Pixels(fd_);
  ASSERT_NE(drawn, empty);

  ASSERT_TRUE(desk().SelectLayer(0).ok());
  ASSERT_TRUE(fd_.Run({"layers.properties"}).ok());
  LayerGeneral g = desk().layer_general()->general();
  ASSERT_TRUE(fv::desk::ParseScale("1:100,000", &g.min_scale).ok());
  ASSERT_TRUE(desk().layer_general()->Set(g).ok());
  EXPECT_TRUE(desk().LayerPropertiesDirty());
  ASSERT_TRUE(desk().ApplyLayerProperties().ok());
  EXPECT_FALSE(desk().LayerPropertiesDirty());
  EXPECT_EQ(Pixels(fd_), empty);  // 1:250,000 is zoomed out beyond 1:100,000

  auto& mv = desk().map_view();
  mv.SetViewport(mv.View().WithCamera(GeoPoint{32.86, -79.95}, 100000, 0));
  const std::vector<uint8_t> in_range = Pixels(fd_);
  rivers_->SetVisible(false);
  EXPECT_NE(in_range, Pixels(fd_));
}

TEST_F(LayerPropertiesTest, GeneralRenamesTheLayerAndDirtiesItsFile) {
  ASSERT_TRUE(fd_.Run({"layers.properties"}).ok());
  const uint64_t rows_before = desk().LayersGeneration();
  LayerGeneral g = desk().layer_general()->general();
  g.name = "Harbour rivers";
  g.description = "Cooper, Ashley and Wando";
  ASSERT_TRUE(desk().layer_general()->Set(g).ok());
  ASSERT_TRUE(desk().ApplyLayerProperties().ok());
  EXPECT_EQ(rivers_->Name(), "Harbour rivers");
  EXPECT_EQ(desk().layers().Rows()[0].name, "Harbour rivers");
  EXPECT_NE(desk().LayersGeneration(), rows_before);
  EXPECT_EQ(desk().properties_title(), "Layer Properties — Harbour rivers");
  EXPECT_TRUE(rivers_->AsPersistence()->is_dirty());

  // Saved, the file keeps them.
  ASSERT_TRUE(fd_.Run({"file.save"}).ok());
  ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  EXPECT_FALSE(rivers_->AsPersistence()->is_dirty());
  FeatureOverlay reopened;
  const fv::Status s = reopened.FileOpen(path_);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(reopened.Name(), "Harbour rivers");
  EXPECT_EQ(reopened.description(), "Cooper, Ashley and Wando");
}

TEST_F(LayerPropertiesTest, StyleAndFieldsAreEditedThroughTheTabs) {
  ASSERT_TRUE(fd_.Run({"layers.properties"}).ok());
  auto style = desk().layer_style();
  fv::FeatureSymbol symbol = style->EntrySymbol(0);
  symbol.line_width_px = 6;
  ASSERT_TRUE(style->SetEntrySymbol(0, symbol).ok());
  fv::FeatureLabel label = style->label();
  label.field = style->LabelFields().at(0).name;
  ASSERT_TRUE(style->SetLabel(label).ok());
  auto fields = desk().layer_fields();
  const size_t count = fields->FieldCount();
  fields->AddField();
  EXPECT_TRUE(desk().LayerPropertiesDirty());

  EXPECT_EQ(desk().LayerFieldsLoss(), "");
  ASSERT_TRUE(desk().ApplyLayerProperties().ok());
  EXPECT_EQ(rivers_->style().symbol.line_width_px, 6);
  EXPECT_EQ(rivers_->style().label.field, label.field);
  EXPECT_EQ(rivers_->layer()->Schema().fields.size(), count + 1);
  EXPECT_TRUE(rivers_->AsPersistence()->is_dirty());
  // The style tab survives the change of fields, over the new schema.
  ASSERT_TRUE(desk().layer_style());
  EXPECT_NE(desk().layer_style(), style);
  EXPECT_EQ(desk().PropertiesTabs().size(), 8u);
  EXPECT_FALSE(desk().LayerPropertiesDirty());

  // Revert drops unapplied edits on every tab.
  symbol.line_width_px = 1;
  ASSERT_TRUE(desk().layer_style()->SetEntrySymbol(0, symbol).ok());
  desk().layer_fields()->AddField();
  ASSERT_TRUE(desk().RevertLayerProperties().ok());
  EXPECT_FALSE(desk().LayerPropertiesDirty());
}

TEST_F(LayerPropertiesTest, StylingOneFeatureTakesTheStyleTabsAway) {
  ASSERT_TRUE(fd_.Run({"layers.properties"}).ok());
  ASSERT_TRUE(desk().SelectFeatureIn(*rivers_, 1));
  ASSERT_TRUE(fd_.Run({"overlay.style_feature"}).ok());
  EXPECT_EQ(fd_.shell().layer_style_shown, 1);
  EXPECT_EQ(desk().PropertiesTabs(),
            (std::vector<PropertiesTab>{PropertiesTab::kSource, PropertiesTab::kGeneral, PropertiesTab::kDisplay,
                                        PropertiesTab::kFields, PropertiesTab::kSelection,
                                        PropertiesTab::kDefinitionQuery}));
  // Closing Properties leaves the feature's style dialog open.
  desk().CloseLayerProperties();
  ASSERT_TRUE(desk().layer_style());
  EXPECT_NE(desk().layer_style()->feature(), 0);
}

TEST_F(LayerPropertiesTest, ClosingTheLayerClosesItsProperties) {
  ASSERT_TRUE(fd_.Run({"layers.properties"}).ok());
  ASSERT_TRUE(fd_.Run({"layers.close"}).ok());
  EXPECT_FALSE(desk().layer_general());
  EXPECT_FALSE(desk().layer_style());
  EXPECT_FALSE(desk().layer_fields());
  EXPECT_FALSE(desk().layer_display());
  EXPECT_FALSE(desk().layer_selection());
  EXPECT_FALSE(desk().layer_query());
}

/// Pixels of `fd`'s frame in exactly `color`.
int CountColor(FakeDesk& fd, fv::FvColor color) {
  const std::vector<uint8_t> px = Pixels(fd);
  int n = 0;
  for (size_t i = 0; i + 3 < px.size(); i += 4)
    if (px[i] == color.r && px[i + 1] == color.g && px[i + 2] == color.b) ++n;
  return n;
}

TEST_F(LayerPropertiesTest, DisplaySetsTheMapTipAndOpacity) {
  ASSERT_TRUE(fd_.Run({kGrid, "layers.properties"}).ok());
  auto display = desk().layer_display();
  ASSERT_TRUE(display);
  EXPECT_TRUE(display->has_opacity());
  EXPECT_EQ(display->fields().size(), 3u);
  fv::desk::LayerDisplay d = display->display();
  d.map_tip_field = "gone";
  EXPECT_EQ(display->Set(d).code, fv::kInvalidArg);
  d.map_tip_field = "flow";
  d.opacity = 101;
  EXPECT_EQ(display->Set(d).code, fv::kInvalidArg);

  // Opacity alone is the workspace's, so the file stays clean.
  d.map_tip_field.clear();
  d.opacity = 60;
  ASSERT_TRUE(display->Set(d).ok());
  ASSERT_TRUE(desk().ApplyLayerProperties().ok());
  EXPECT_EQ(rivers_->opacity(), 60);
  EXPECT_FALSE(rivers_->AsPersistence()->is_dirty());

  d.map_tip_field = "flow";
  ASSERT_TRUE(display->Set(d).ok());
  ASSERT_TRUE(desk().ApplyLayerProperties().ok());
  EXPECT_TRUE(rivers_->AsPersistence()->is_dirty());
  Pixels(fd_);
  double x = 0, y = 0;
  ASSERT_TRUE(rivers_->last_projection().GeoToSurface({32.85, -79.93}, &x, &y).ok());
  desk().OverlayHover(x, y);
  EXPECT_EQ(desk().HoverToolTip(), "130");
}

TEST_F(LayerPropertiesTest, SelectByAttributesSelectsAndTheLayersColourDrawsIt) {
  ASSERT_TRUE(fd_.Run({kGrid}).ok());  // the rivers alone
  EXPECT_FALSE(desk().commands().IsEnabled("layers.clear_selection"));
  ASSERT_TRUE(fd_.Run({"layers.select_by_attributes"}).ok());
  EXPECT_EQ(fd_.shell().select_by_attributes_shown, 1);
  auto q = desk().select_query();
  ASSERT_TRUE(q);
  const size_t c = q->AddClause();
  ASSERT_TRUE(q->SetClause(c, {"depth", fv::desk::QueryOp::kGreater, "7"}).ok());
  size_t selected = 0;
  ASSERT_TRUE(desk().ApplySelectByAttributes(fv::desk::SelectionMethod::kNew, &selected).ok());
  EXPECT_EQ(selected, 2u);
  EXPECT_EQ(rivers_->selection(), (std::vector<int64_t>{1, 3}));
  EXPECT_TRUE(desk().commands().IsEnabled("layers.clear_selection"));

  const fv::FvColor yellow{255, 200, 0, 255}, cyan{0, 200, 255, 255};
  const int halo = CountColor(fd_, yellow);
  EXPECT_GT(halo, 50);
  EXPECT_EQ(CountColor(fd_, cyan), 0);

  ASSERT_TRUE(fd_.Run({"layers.properties"}).ok());
  auto sel = desk().layer_selection();
  ASSERT_TRUE(sel);
  EXPECT_EQ(sel->selected_count(), 2u);
  fv::desk::LayerSelection s = sel->selection();
  EXPECT_TRUE(s.use_default);
  s.use_default = false;
  s.style.color = cyan;
  s.style.width_px = 100;
  EXPECT_EQ(sel->Set(s).code, fv::kInvalidArg);
  s.style.width_px = 4;
  ASSERT_TRUE(sel->Set(s).ok());
  ASSERT_TRUE(desk().ApplyLayerProperties().ok());
  EXPECT_TRUE(rivers_->AsPersistence()->is_dirty());
  EXPECT_EQ(CountColor(fd_, yellow), 0);
  EXPECT_EQ(CountColor(fd_, cyan), halo);

  // The other methods combine with the selection.
  ASSERT_TRUE(q->SetClause(c, {"name", fv::desk::QueryOp::kEqual, "Ashley River"}).ok());
  ASSERT_TRUE(desk().ApplySelectByAttributes(fv::desk::SelectionMethod::kAdd, &selected).ok());
  EXPECT_EQ(selected, 3u);
  ASSERT_TRUE(q->SetClause(c, {"flow", fv::desk::QueryOp::kLess, "100"}).ok());
  ASSERT_TRUE(desk().ApplySelectByAttributes(fv::desk::SelectionMethod::kRemove, &selected).ok());
  EXPECT_EQ(rivers_->selection(), (std::vector<int64_t>{1}));
  ASSERT_TRUE(desk().ApplySelectByAttributes(fv::desk::SelectionMethod::kWithin, &selected).ok());
  EXPECT_EQ(selected, 0u);
  ASSERT_TRUE(desk().ApplySelectByAttributes(fv::desk::SelectionMethod::kNew).ok());
  ASSERT_TRUE(fd_.Run({"layers.clear_selection"}).ok());
  EXPECT_TRUE(rivers_->selection().empty());

  ASSERT_TRUE(fd_.Run({"layers.close"}).ok());
  EXPECT_FALSE(desk().select_query());
}

TEST_F(LayerPropertiesTest, ADefinitionQueryFiltersTheMapAndTableAndIsSavedInTheFile) {
  ASSERT_TRUE(fd_.Run({kGrid}).ok());
  const std::vector<uint8_t> all = Pixels(fd_);
  ASSERT_TRUE(fd_.Run({"overlay.attribute_table", "layers.properties"}).ok());
  ASSERT_EQ(desk().attribute_table()->RowCount(), 3u);
  auto q = desk().layer_query();
  ASSERT_TRUE(q);
  EXPECT_TRUE(q->query().clauses.empty());
  const size_t c = q->AddClause();
  ASSERT_TRUE(q->SetClause(c, {"name", fv::desk::QueryOp::kNotEqual, "Cooper River"}).ok());
  EXPECT_TRUE(desk().LayerPropertiesDirty());
  ASSERT_TRUE(desk().ApplyLayerProperties().ok());
  EXPECT_FALSE(desk().LayerPropertiesDirty());
  EXPECT_EQ(rivers_->definition_query().text(), R"(["!=",["get","name"],"Cooper River"])");
  EXPECT_TRUE(rivers_->AsPersistence()->is_dirty());
  EXPECT_NE(Pixels(fd_), all);
  EXPECT_EQ(rivers_->features_drawn(), 2u);
  EXPECT_EQ(desk().attribute_table()->RowCount(), 2u);

  // An incomplete clause stops Apply and leaves the layer's query.
  ASSERT_TRUE(q->SetClause(c, {"depth", fv::desk::QueryOp::kLess, ""}).ok());
  EXPECT_EQ(desk().ApplyLayerProperties().code, fv::kInvalidArg);
  EXPECT_FALSE(rivers_->definition_query().empty());
  ASSERT_TRUE(desk().RevertLayerProperties().ok());

  ASSERT_TRUE(fd_.Run({"file.save"}).ok());
  ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  FeatureOverlay reopened;
  ASSERT_TRUE(reopened.FileOpen(path_).ok());
  EXPECT_EQ(reopened.layer()->Count(), 3u);
  EXPECT_EQ(reopened.definition_query().text(), rivers_->definition_query().text());

  // Clearing the query shows every feature again.
  ASSERT_TRUE(q->RemoveClause(0).ok());
  ASSERT_TRUE(desk().ApplyLayerProperties().ok());
  EXPECT_TRUE(rivers_->definition_query().empty());
  EXPECT_EQ(Pixels(fd_), all);
  EXPECT_EQ(desk().attribute_table()->RowCount(), 3u);
}

}  // namespace

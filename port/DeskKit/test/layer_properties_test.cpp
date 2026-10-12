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
            (std::vector<PropertiesTab>{PropertiesTab::kSource, PropertiesTab::kGeneral, PropertiesTab::kSymbology,
                                        PropertiesTab::kLabels, PropertiesTab::kFields}));
  EXPECT_EQ(desk().properties_tab(), PropertiesTab::kGeneral);
  ASSERT_TRUE(desk().layer_general());
  EXPECT_EQ(desk().layer_general()->general().name, rivers_->Name());
  ASSERT_TRUE(desk().layer_style());
  EXPECT_EQ(desk().layer_style()->feature(), 0);
  ASSERT_TRUE(desk().layer_fields());
  EXPECT_FALSE(desk().LayerPropertiesDirty());
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

TEST_F(LayerPropertiesTest, ALayerWithoutFeaturesHasSourceAndGeneral) {
  ASSERT_TRUE(desk().SelectLayer(1).ok());  // the grid, below the rivers
  ASSERT_NE(desk().overlays().current(), rivers_.get());
  ASSERT_TRUE(fd_.Run({"layers.properties"}).ok());
  EXPECT_EQ(desk().PropertiesTabs(), (std::vector<PropertiesTab>{PropertiesTab::kSource, PropertiesTab::kGeneral}));
  EXPECT_FALSE(desk().layer_style());

  // A source's Properties is the Source tab alone.
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
  EXPECT_EQ(desk().PropertiesTabs().size(), 5u);
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
            (std::vector<PropertiesTab>{PropertiesTab::kSource, PropertiesTab::kGeneral, PropertiesTab::kFields}));
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
}

}  // namespace

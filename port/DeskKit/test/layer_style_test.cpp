// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "fv_desk_fake.h"
#include "fv_desk_layer_style.h"
#include "fvkit/feature/geojson.h"
#include "fvkit/overlay/feature_overlay.h"
#include "scratch.h"

namespace {

using fv::FeatureOverlay;
using fv::FeatureRenderer;
using fv::FeatureStyle;
using fv::desk::Desk;
using fv::desk::FakeDesk;
using fv::desk::LayerStyleModel;
using fv::desk::SymbolProperty;

/// Pizza restaurants with a style pick-list.
std::shared_ptr<fv::MemoryFeatureLayer> PizzaLayer() {
  fv::FeatureSchema s;
  s.geometry_type = fv::GeometryType::kPoint;
  fv::FieldDef name;
  name.name = "name";
  name.title = "Name";
  fv::FieldDef style;
  style.name = "style";
  style.title = "Style";
  style.role = fv::FieldRole::kEnumeration;
  style.choices = {{"ny", "New York"}, {"neapolitan", "Neapolitan"}, {"detroit", "Detroit"}};
  fv::FieldDef rating;
  rating.name = "rating";
  rating.type = fv::FieldType::kReal;
  s.fields = {name, style, rating};
  auto layer = std::make_shared<fv::MemoryFeatureLayer>("Pizza", s);
  const struct {
    double lat, lon;
    const char* name;
    const char* style;
    double rating;
  } rows[] = {{32.78, -79.93, "Andolini's", "ny", 4.0},
              {32.80, -79.95, "Paulie Gee's", "neapolitan", 4.5},
              {32.79, -79.94, "Slice Box", "detroit", 3.0}};
  for (const auto& r : rows) {
    fv::Feature f;
    f.geometry = fv::Geometry::Point({r.lat, r.lon});
    f.values = {std::string(r.name), std::string(r.style), r.rating};
    layer->Insert(&f);
  }
  return layer;
}

/// A FakeDesk with one feature layer opened from a file, current.
class LayerStyleTest : public ::testing::Test {
 protected:
  void Open(const std::string& path) {
    fd_.shell().files_to_open = {path};
    ASSERT_TRUE(fd_.Run({"overlay.open"}).ok());
    ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
    auto found = desk().overlays().OfType(FeatureOverlay::kTypeId);
    ASSERT_FALSE(found.empty());
    overlay_ = std::static_pointer_cast<FeatureOverlay>(found.back());
  }
  void OpenPizza() {
    ASSERT_TRUE(fv::WriteGeoJsonFile(*PizzaLayer(), scratch_.Path("pizza.geojson")).ok());
    Open(scratch_.Path("pizza.geojson"));
  }
  /// Saves the current overlay as `name` in `format`, closes it and opens the file again.
  void SaveCloseReopen(const std::string& name, int format) {
    fd_.shell().save_spec = scratch_.Path(name);
    fd_.shell().save_format = format;
    ASSERT_TRUE(fd_.Run({"file.save_as"}).ok());
    ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
    ASSERT_TRUE(fd_.Run({"file.close"}).ok());
    overlay_.reset();
    Open(scratch_.Path(name));
  }
  Desk& desk() { return fd_.desk(); }
  LayerStyleModel& style() { return *desk().layer_style(); }

  fv::desk::test::Scratch scratch_;
  FakeDesk fd_{400, 400};
  std::shared_ptr<FeatureOverlay> overlay_;
};

TEST_F(LayerStyleTest, CommandsNeedAFeatureLayerAndASelection) {
  EXPECT_FALSE(desk().commands().IsEnabled("overlay.style"));
  OpenPizza();
  EXPECT_TRUE(desk().commands().IsEnabled("overlay.style"));
  EXPECT_FALSE(desk().commands().IsEnabled("overlay.style_feature"));
  overlay_->SetSelected(2);
  EXPECT_TRUE(desk().commands().IsEnabled("overlay.style_feature"));
  ASSERT_TRUE(fd_.Run({"overlay.style"}).ok());
  EXPECT_EQ(fd_.shell().layer_style_shown, 1);
  ASSERT_NE(desk().layer_style(), nullptr);
  EXPECT_EQ(style().title(), "Layer Style — Pizza");
  EXPECT_EQ(style().EntryCount(), 1u);
  EXPECT_EQ(style().EntryLabel(0), "All features");
}

TEST_F(LayerStyleTest, ThePizzaLayerCategorisedByStyleSavesAndReopensIdentical) {
  OpenPizza();
  ASSERT_TRUE(fd_.Run({"overlay.style"}).ok());
  ASSERT_TRUE(style().SetRenderer(FeatureRenderer::kCategorised).ok());
  EXPECT_EQ(style().field(), "name");  // the first usable field
  ASSERT_EQ(style().CandidateFields().size(), 3u);
  ASSERT_TRUE(style().SetField("style").ok());
  ASSERT_EQ(style().EntryCount(), 4u);
  EXPECT_EQ(style().EntryLabel(0), "New York");
  EXPECT_EQ(style().EntryValue(1), "neapolitan");
  EXPECT_TRUE(style().EntryIsOther(3));
  EXPECT_EQ(style().EntryLabel(3), "Other");

  fv::FeatureSymbol detroit = style().EntrySymbol(2);
  ASSERT_TRUE(fv::desk::SetSymbolChoice(&detroit, SymbolProperty::kMarkerShape, 1).ok());
  ASSERT_TRUE(fv::desk::SetSymbolNumber(&detroit, SymbolProperty::kMarkerSize, 15).ok());
  EXPECT_FALSE(fv::desk::SetSymbolNumber(&detroit, SymbolProperty::kMarkerSize, 100).ok());
  ASSERT_TRUE(style().SetEntrySymbol(2, detroit).ok());
  fv::FeatureLabel label = style().label();
  label.field = "name";
  ASSERT_TRUE(style().SetLabel(label).ok());
  EXPECT_TRUE(style().dirty());
  EXPECT_FALSE(overlay_->is_dirty());

  ASSERT_TRUE(desk().ApplyLayerStyle().ok());
  EXPECT_FALSE(style().dirty());
  EXPECT_TRUE(overlay_->is_dirty());
  const FeatureStyle applied = overlay_->style();
  EXPECT_EQ(applied.renderer, FeatureRenderer::kCategorised);
  EXPECT_EQ(applied.categories[2].symbol.marker, fv::FeatureMarker::kSquare);

  SaveCloseReopen("pizza.gpkg", FeatureOverlay::kFormatGeoPackage);
  EXPECT_EQ(desk().layer_style(), nullptr);  // closed with its overlay
  EXPECT_TRUE(overlay_->style() == applied);
}

TEST_F(LayerStyleTest, TheRiversGraduatedByFlowSaveAndReopenIdentical) {
  const std::string rivers = scratch_.Path("rivers.geojson");
  std::filesystem::copy_file(FV_FEATURE_RIVERS, rivers);
  Open(rivers);
  ASSERT_TRUE(fd_.Run({"overlay.style"}).ok());
  ASSERT_TRUE(style().SetRenderer(FeatureRenderer::kGraduated).ok());
  EXPECT_EQ(style().field(), "depth");  // the first number field
  ASSERT_EQ(style().CandidateFields().size(), 2u);
  fv::GraduatedOptions opt = style().graduated();
  opt.classes = 3;
  opt.vary = fv::GraduatedVary::kSize;
  opt.min_px = 2;
  opt.max_px = 8;
  style().SetGraduated(opt);
  ASSERT_TRUE(style().SetField("flow").ok());
  ASSERT_EQ(style().EntryCount(), 4u);
  EXPECT_EQ(style().EntryLabel(0), "25 \xe2\x80\x93 60");
  EXPECT_EQ(style().EntryLabel(2), "\xe2\x89\xa5 95");
  EXPECT_DOUBLE_EQ(style().EntrySymbol(2).line_width_px, 8.0);

  // A bound typed out of order re-sorts the classes.
  EXPECT_FALSE(style().SetEntryValue(1, "95").ok());
  EXPECT_FALSE(style().SetEntryValue(1, "lots").ok());
  ASSERT_TRUE(style().SetEntryValue(2, "50").ok());
  EXPECT_EQ(style().EntryValue(1), "50");
  EXPECT_DOUBLE_EQ(style().EntrySymbol(1).line_width_px, 8.0);
  ASSERT_TRUE(style().SetEntryLabel(1, "  fast ").ok());
  EXPECT_EQ(style().EntryLabel(1), "fast");
  EXPECT_FALSE(style().RemoveEntry(3).ok());  // Other stays
  ASSERT_TRUE(style().AddEntry().ok());
  EXPECT_EQ(style().EntryValue(3), "70");
  ASSERT_TRUE(style().RemoveEntry(3).ok());

  ASSERT_TRUE(desk().ApplyLayerStyle().ok());
  const FeatureStyle applied = overlay_->style();
  SaveCloseReopen("rivers-styled.geojson", FeatureOverlay::kFormatGeoJson);
  EXPECT_TRUE(overlay_->style() == applied);
  SaveCloseReopen("rivers-styled.gpkg", FeatureOverlay::kFormatGeoPackage);
  EXPECT_TRUE(overlay_->style() == applied);
}

TEST_F(LayerStyleTest, StyleThisFeatureSetsAndClearsAnOverride) {
  OpenPizza();
  overlay_->SetSelected(2);
  ASSERT_TRUE(fd_.Run({"overlay.style_feature"}).ok());
  ASSERT_NE(desk().layer_style(), nullptr);
  EXPECT_EQ(style().title(), "Feature Style — Paulie Gee's");
  EXPECT_EQ(style().feature(), 2);
  EXPECT_EQ(style().EntryCount(), 1u);
  EXPECT_FALSE(style().SetRenderer(FeatureRenderer::kCategorised).ok());
  EXPECT_FALSE(style().has_override());
  // Points only: the marker properties.
  EXPECT_EQ(fv::desk::SymbolPropertiesFor(style().geometry()).size(), 5u);

  fv::FeatureSymbol sym = style().EntrySymbol(0);
  ASSERT_TRUE(fv::desk::SetSymbolColor(&sym, SymbolProperty::kMarkerFill, {0, 200, 0, 255}).ok());
  ASSERT_TRUE(style().SetEntrySymbol(0, sym).ok());
  EXPECT_TRUE(style().has_override());
  ASSERT_TRUE(desk().ApplyLayerStyle().ok());
  ASSERT_EQ(overlay_->style().overrides.count(2), 1u);
  EXPECT_EQ(overlay_->style().overrides.at(2).marker_fill.g, 200);

  ASSERT_TRUE(style().ClearOverride().ok());
  EXPECT_TRUE(style().dirty());
  ASSERT_TRUE(style().Revert().ok());
  EXPECT_TRUE(style().has_override());
  ASSERT_TRUE(style().ClearOverride().ok());
  ASSERT_TRUE(desk().ApplyLayerStyle().ok());
  EXPECT_TRUE(overlay_->style().overrides.empty());

  fv::PixelBuffer swatch;
  ASSERT_TRUE(style().RenderSwatch(0, 24, 16, 1.0, &swatch).ok());
  EXPECT_EQ(swatch.Width(), 24);
}

TEST_F(LayerStyleTest, ClosingTheOverlayClosesTheDialog) {
  OpenPizza();
  ASSERT_TRUE(fd_.Run({"overlay.style"}).ok());
  const uint64_t gen = desk().FeatureViewsGeneration();
  ASSERT_TRUE(fd_.Run({"file.close"}).ok());
  EXPECT_EQ(desk().layer_style(), nullptr);
  EXPECT_GT(desk().FeatureViewsGeneration(), gen);
  EXPECT_EQ(desk().ApplyLayerStyle().code, fv::kNotFound);
}

}  // namespace

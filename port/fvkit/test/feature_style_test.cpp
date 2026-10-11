// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/feature/style.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#include <sqlite3.h>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/feature/geojson.h"
#include "fvkit/feature/geopackage.h"
#include "fvkit/overlay/feature_overlay.h"

namespace {

using fv::FeatureRenderer;
using fv::FeatureStyle;
using fv::FeatureSymbol;
using fv::FvColor;

namespace fs = std::filesystem;

const std::string kRivers = FV_FEATURE_RIVERS;

std::string Temp(const std::string& name) {
  const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
  const fs::path p = fs::temp_directory_path() / (std::string("fv_fs_") + info->name() + "_" + name);
  std::error_code ec;
  fs::remove(p, ec);
  return p.string();
}

fv::Feature PointAt(double lat, double lon, std::vector<fv::FieldValue> values) {
  fv::Feature f;
  f.geometry = fv::Geometry::Point({lat, lon});
  f.values = std::move(values);
  return f;
}

/// Pizza restaurants: name, phone and a style picked from a list.
std::shared_ptr<fv::MemoryFeatureLayer> Pizza() {
  fv::FeatureSchema schema;
  schema.geometry_type = fv::GeometryType::kPoint;
  fv::FieldDef name;
  name.name = "name";
  fv::FieldDef phone;
  phone.name = "phone";
  phone.role = fv::FieldRole::kPhone;
  fv::FieldDef style;
  style.name = "style";
  style.role = fv::FieldRole::kEnumeration;
  style.choices = {{"neapolitan", "Neapolitan"}, {"new-york", "New York"}, {"detroit", "Detroit"}};
  schema.fields = {name, phone, style};
  auto layer = std::make_shared<fv::MemoryFeatureLayer>("Pizza", schema);
  fv::Feature a = PointAt(32.78, -79.94, {std::string("Monza"), std::string("843-555-0101"), std::string("neapolitan")});
  fv::Feature b = PointAt(32.80, -79.96, {std::string("Slice"), std::string("843-555-0102"), std::string("new-york")});
  fv::Feature c = PointAt(32.76, -79.92, {std::string("Corner"), fv::FieldValue{}, fv::FieldValue{}});
  EXPECT_TRUE(layer->Insert(&a).ok());
  EXPECT_TRUE(layer->Insert(&b).ok());
  EXPECT_TRUE(layer->Insert(&c).ok());
  return layer;
}

/// The pizza layer categorised by style, with Monza (id 1) overridden as a square.
FeatureStyle PizzaStyle(const fv::IFeatureLayer& layer) {
  FeatureStyle style;
  style.renderer = FeatureRenderer::kCategorised;
  style.field = "style";
  style.symbol.marker_fill = FvColor{128, 128, 128, 255};
  EXPECT_TRUE(fv::ClassifyCategories(layer, 2, style.symbol, &style.categories).ok());
  FeatureSymbol special = style.categories[0].symbol;
  special.marker = fv::FeatureMarker::kSquare;
  special.marker_size_px = 14;
  style.overrides[1] = special;
  style.label.field = "name";
  return style;
}

/// The rivers graduated by flow, in three equal-interval width classes.
FeatureStyle RiverStyle(const fv::IFeatureLayer& layer) {
  FeatureStyle style;
  style.renderer = FeatureRenderer::kGraduated;
  style.field = "flow";
  fv::GraduatedOptions opt;
  opt.classes = 3;
  opt.vary = fv::GraduatedVary::kSize;
  opt.min_px = 1;
  opt.max_px = 7;
  EXPECT_TRUE(fv::ClassifyGraduated(layer, layer.Schema().Find("flow"), style.symbol, opt, &style.classes).ok());
  style.classes[2].label = "big";
  return style;
}

TEST(FeatureStyle, CategoriesFollowTheChoiceListAndOtherCatchesTheRest) {
  auto layer = Pizza();
  const FeatureStyle style = PizzaStyle(*layer);
  ASSERT_EQ(style.categories.size(), 3u);  // every choice, used or not
  EXPECT_EQ(style.categories[0].value, "neapolitan");
  EXPECT_EQ(style.categories[1].label, "New York");
  EXPECT_EQ(style.categories[2].value, "detroit");
  EXPECT_NE(style.categories[0].symbol.marker_fill.r, style.categories[1].symbol.marker_fill.r);

  std::vector<fv::Feature> all;
  ASSERT_TRUE(layer->All(&all).ok());
  EXPECT_EQ(style.SymbolFor(all[0], 2).marker, fv::FeatureMarker::kSquare);  // the override
  EXPECT_EQ(&style.SymbolFor(all[1], 2), &style.categories[1].symbol);
  EXPECT_EQ(&style.SymbolFor(all[2], 2), &style.symbol);  // null style
  EXPECT_EQ(&style.SymbolFor(all[1], -1), &style.symbol);  // field gone from the schema
}

TEST(FeatureStyle, GraduatedClassesStepAtTheirLowerBound) {
  std::shared_ptr<fv::MemoryFeatureLayer> rivers;
  ASSERT_TRUE(fv::ReadGeoJsonFile(kRivers, &rivers).ok());
  const FeatureStyle style = RiverStyle(*rivers);
  // Flows 130, 25, 40: equal intervals from 25 to 130.
  ASSERT_EQ(style.classes.size(), 3u);
  EXPECT_DOUBLE_EQ(style.classes[0].lower, 25.0);
  EXPECT_DOUBLE_EQ(style.classes[1].lower, 60.0);
  EXPECT_DOUBLE_EQ(style.classes[2].lower, 95.0);
  EXPECT_DOUBLE_EQ(style.classes[0].symbol.line_width_px, 1.0);
  EXPECT_DOUBLE_EQ(style.classes[2].symbol.line_width_px, 7.0);
  EXPECT_EQ(fv::FeatureClassLabel(style, 0), "25 \xe2\x80\x93 60");
  EXPECT_EQ(fv::FeatureClassLabel(style, 2), "big");

  const int flow = rivers->Schema().Find("flow");
  fv::Feature f;
  f.values.resize(rivers->Schema().fields.size());
  f.values[flow] = 60.0;
  EXPECT_EQ(&style.SymbolFor(f, flow), &style.classes[1].symbol);
  f.values[flow] = 59.999;
  EXPECT_EQ(&style.SymbolFor(f, flow), &style.classes[0].symbol);
  f.values[flow] = 10.0;
  EXPECT_EQ(&style.SymbolFor(f, flow), &style.symbol);  // below every class
  f.values[flow] = int64_t{500};
  EXPECT_EQ(&style.SymbolFor(f, flow), &style.classes[2].symbol);

  fv::GraduatedOptions quantile;
  quantile.classes = 5;  // only three distinct values
  quantile.mode = fv::GraduatedMode::kQuantile;
  std::vector<fv::FeatureClass> q;
  ASSERT_TRUE(fv::ClassifyGraduated(*rivers, flow, style.symbol, quantile, &q).ok());
  ASSERT_EQ(q.size(), 3u);
  EXPECT_DOUBLE_EQ(q[1].lower, 40.0);
  EXPECT_EQ(q[0].symbol.line_color.r, quantile.from.r);
  EXPECT_EQ(q[2].symbol.line_color.r, quantile.to.r);

  EXPECT_EQ(fv::ClassifyGraduated(*rivers, rivers->Schema().Find("name"), style.symbol, quantile, &q).code,
            fv::kInvalidArg);
}

TEST(FeatureStyle, JsonIsAMapLibreDocumentThatRoundTrips) {
  auto pizza = Pizza();
  std::shared_ptr<fv::MemoryFeatureLayer> rivers;
  ASSERT_TRUE(fv::ReadGeoJsonFile(kRivers, &rivers).ok());
  FeatureStyle single;
  single.symbol.dash = "dash";
  single.symbol.casing_color = FvColor{255, 255, 255, 200};
  single.symbol.casing_px = 2;
  for (const FeatureStyle& style : {single, PizzaStyle(*pizza), RiverStyle(*rivers)}) {
    std::string text;
    ASSERT_TRUE(fv::WriteFeatureStyleJson(style, &text).ok());
    FeatureStyle back;
    const fv::Status s = fv::ReadFeatureStyleJson(text, &back);
    ASSERT_TRUE(s.ok()) << s.message;
    EXPECT_TRUE(back == style) << text;
  }
  std::string text;
  ASSERT_TRUE(fv::WriteFeatureStyleJson(PizzaStyle(*pizza), &text).ok());
  EXPECT_NE(text.find("\"version\": 8"), std::string::npos);
  EXPECT_NE(text.find("\"match\""), std::string::npos);
  EXPECT_NE(text.find("\"id\""), std::string::npos);
  ASSERT_TRUE(fv::WriteFeatureStyleJson(RiverStyle(*rivers), &text).ok());
  EXPECT_NE(text.find("\"step\""), std::string::npos);
  // A constant property stays a constant.
  EXPECT_NE(text.find("\"circle-stroke-color\": \"#ffffff\""), std::string::npos);
  EXPECT_EQ(text.find("fv-halo"), std::string::npos);
  ASSERT_TRUE(fv::WriteFeatureStyleJson(RiverStyle(*rivers), &text, 3).ok());
  EXPECT_NE(text.find("\"fv-halo-line\""), std::string::npos);
  // The casing layer is as wide as the whole casing: 2 + 2 x 2.
  ASSERT_TRUE(fv::WriteFeatureStyleJson(single, &text).ok());
  const size_t casing = text.find("\"fv-line-casing\"");
  ASSERT_NE(casing, std::string::npos);
  EXPECT_NE(text.find("\"line-width\": 6", casing), std::string::npos) << text;
}

TEST(FeatureStyle, JsonRejectsWhatItCannotEvaluate) {
  FeatureStyle style;
  EXPECT_EQ(fv::ReadFeatureStyleJson("not json", &style).code, fv::kInvalidArg);
  EXPECT_EQ(fv::ReadFeatureStyleJson(R"j({"version":8,"layers":[{"id":"fv-line","type":"line",
      "paint":{"line-width":["interpolate",["linear"],["zoom"],5,1,10,4]}}]})j", &style).code,
            fv::kInvalidArg);
  EXPECT_EQ(fv::ReadFeatureStyleJson(R"j({"metadata":{"fv:renderer":"graduated"}})j", &style).code,
            fv::kInvalidArg);  // no field
  // Hand-written: a categorised colour with array labels.
  const fv::Status s = fv::ReadFeatureStyleJson(R"j({"version":8,
      "metadata":{"fv:renderer":"categorised","fv:field":"kind",
                  "fv:categories":[{"value":"a"},{"value":"b"}]},
      "layers":[{"id":"fv-line","type":"line","paint":{
        "line-color":["match",["get","kind"],["a","b"],"rgba(0, 0, 255, 0.5)","#f00"]}}]})j", &style);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(style.categories[1].symbol.line_color.b, 255);
  EXPECT_EQ(style.categories[1].symbol.line_color.a, 128);
  EXPECT_EQ(style.symbol.line_color.r, 255);

  FeatureStyle unordered;
  unordered.renderer = FeatureRenderer::kGraduated;
  unordered.field = "flow";
  unordered.classes.resize(2);
  unordered.classes[0].lower = 5;
  unordered.classes[0].symbol.line_width_px = 3;
  std::string text;
  EXPECT_EQ(fv::WriteFeatureStyleJson(unordered, &text).code, fv::kInvalidArg);
}

TEST(FeatureStyle, SavedAndReopenedIdenticalInGeoPackageAndGeoJson) {
  // The pizza layer categorised by style.
  {
    fv::FeatureOverlay overlay;
    overlay.SetLayer(Pizza());
    const FeatureStyle style = PizzaStyle(*overlay.layer());
    overlay.SetStyle(style);
    const std::string gpkg = Temp("pizza.gpkg");
    ASSERT_TRUE(overlay.FileSaveAs(gpkg, fv::FeatureOverlay::kFormatGeoPackage).ok());
    fv::FeatureOverlay reopened;
    ASSERT_TRUE(reopened.FileOpen(gpkg).ok());
    EXPECT_TRUE(reopened.style() == style);

    // Saving again to the open file updates its one style row.
    FeatureStyle changed = style;
    changed.categories[2].symbol.marker_size_px = 20;
    reopened.SetStyle(changed);
    ASSERT_TRUE(reopened.FileSaveAs(gpkg, fv::FeatureOverlay::kFormatGeoPackage).ok());
    reopened.SetLayer(nullptr);
    fv::FeatureOverlay third;
    ASSERT_TRUE(third.FileOpen(gpkg).ok());
    EXPECT_TRUE(third.style() == changed);
    third.SetLayer(nullptr);

    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open_v2(gpkg.c_str(), &db, SQLITE_OPEN_READONLY, nullptr), SQLITE_OK);
    sqlite3_stmt* st = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db,
                                 "SELECT COUNT(*), MAX(useAsDefault), MAX(f_table_name), "
                                 "(SELECT data_type FROM gpkg_contents WHERE table_name = 'layer_styles') "
                                 "FROM layer_styles",
                                 -1, &st, nullptr),
              SQLITE_OK);
    ASSERT_EQ(sqlite3_step(st), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(st, 0), 1);
    EXPECT_EQ(sqlite3_column_int(st, 1), 0);  // QGIS must not apply a row it cannot read
    EXPECT_STREQ(reinterpret_cast<const char*>(sqlite3_column_text(st, 2)), fs::path(gpkg).stem().string().c_str());
    EXPECT_STREQ(reinterpret_cast<const char*>(sqlite3_column_text(st, 3)), "attributes");
    sqlite3_finalize(st);
    sqlite3_close(db);
  }
  // The rivers graduated by flow.
  {
    fv::FeatureOverlay overlay;
    ASSERT_TRUE(overlay.FileOpen(kRivers).ok());
    EXPECT_TRUE(overlay.style() == FeatureStyle{});  // the sample carries no style
    const FeatureStyle style = RiverStyle(*overlay.layer());
    overlay.SetStyle(style);
    const std::string json = Temp("rivers.geojson");
    ASSERT_TRUE(overlay.FileSaveAs(json, fv::FeatureOverlay::kFormatGeoJson).ok());
    EXPECT_TRUE(overlay.style() == style);
    const std::string gpkg = Temp("rivers.gpkg");
    ASSERT_TRUE(overlay.FileSaveAs(gpkg, fv::FeatureOverlay::kFormatGeoPackage).ok());
    for (const std::string& path : {json, gpkg}) {
      fv::FeatureOverlay reopened;
      ASSERT_TRUE(reopened.FileOpen(path).ok()) << path;
      EXPECT_TRUE(reopened.style() == style) << path;
    }
  }
}

TEST(FeatureStyle, ABadStoredStyleOpensWithTheDefault) {
  const std::string path = Temp("bad.geojson");
  {
    std::shared_ptr<fv::MemoryFeatureLayer> rivers;
    ASSERT_TRUE(fv::ReadGeoJsonFile(kRivers, &rivers).ok());
    ASSERT_TRUE(fv::WriteGeoJsonFile(*rivers, path,
                                     R"j({"metadata":{"fv:renderer":"stripes"}})j").ok());
  }
  fv::FeatureOverlay overlay;
  ASSERT_TRUE(overlay.FileOpen(path).ok());
  EXPECT_TRUE(overlay.style() == FeatureStyle{});
  EXPECT_EQ(fv::WriteGeoJsonFile(*overlay.layer(), path, "[1]").code, fv::kInvalidArg);
}

TEST(FeatureStyle, CategoriesDrawInTheirOwnColours) {
  fv::FeatureOverlay overlay;
  overlay.SetLayer(Pizza());
  FeatureStyle style = PizzaStyle(*overlay.layer());
  style.overrides.clear();
  style.label.field.clear();
  overlay.SetStyle(style);
  fv::MapProjection proj;
  proj.SetSurfaceSize(400, 400);
  proj.SetCenter({32.78, -79.94});
  proj.SetScale(250000.0);
  fv::CpuCanvas canvas(400, 400);
  canvas.Clear({255, 255, 255, 255});
  ASSERT_TRUE(overlay.OnDraw(proj, canvas).ok());
  EXPECT_EQ(overlay.features_drawn(), 3u);
  auto colour_at = [&](double lat, double lon) {
    double x = 0, y = 0;
    EXPECT_TRUE(proj.GeoToSurface({lat, lon}, &x, &y).ok());
    const unsigned char* p = canvas.Buffer().Row(static_cast<int>(y)) + 4 * static_cast<int>(x);
    return FvColor{p[0], p[1], p[2], p[3]};
  };
  const FvColor neapolitan = colour_at(32.78, -79.94);
  const FvColor new_york = colour_at(32.80, -79.96);
  const FvColor other = colour_at(32.76, -79.92);
  EXPECT_EQ(neapolitan.r, style.categories[0].symbol.marker_fill.r);
  EXPECT_EQ(new_york.g, style.categories[1].symbol.marker_fill.g);
  EXPECT_EQ(other.r, 128);
}

TEST(FeatureStyle, SwatchesInkEachGeometryKind) {
  FeatureSymbol sym;
  for (fv::GeometryType g : {fv::GeometryType::kPoint, fv::GeometryType::kLineString,
                             fv::GeometryType::kPolygon, fv::GeometryType::kGeometry}) {
    fv::CpuCanvas c(32, 20);
    c.Clear({255, 255, 255, 0});
    ASSERT_TRUE(fv::DrawFeatureSwatch(c, sym, g, 2.0).ok());
    int inked = 0;
    for (int y = 0; y < 20; ++y)
      for (int x = 0; x < 32; ++x) inked += c.Buffer().Row(y)[4 * x + 3] != 0;
    EXPECT_GT(inked, 20) << static_cast<int>(g);
  }
}

}  // namespace

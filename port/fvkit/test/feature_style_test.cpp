// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/feature/style.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>
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

// MARK: Rules

/// A style.json written by hand: four rule types, filters, zoom expressions,
/// a hidden rule and members fvkit does not use.
const char kHandStyle[] = R"j({
  "version": 8,
  "name": "Pizza by hand",
  "sources": {"pizza": {"type": "vector", "url": "mbtiles://pizza.mbtiles"}},
  "layers": [
    {"id": "areas", "type": "fill", "source": "pizza", "source-layer": "places",
     "filter": ["==", ["geometry-type"], "Polygon"], "paint": {"fill-color": "#cce0ff", "fill-opacity": 0.5}},
    {"id": "roads", "type": "line", "source": "pizza", "source-layer": "places", "minzoom": 8,
     "paint": {"line-color": "#888888",
               "line-width": ["interpolate", ["linear"], ["zoom"], 8, 1, 16, 6]}},
    {"id": "naples", "type": "circle", "source": "pizza", "source-layer": "places",
     "filter": ["all", ["==", ["get", "style"], "neapolitan"], ["has", "name"]],
     "paint": {"circle-radius": 7, "circle-color": "#d03020"},
     "metadata": {"fv:legend": "Neapolitan", "note": "kept"}},
    {"id": "others", "type": "circle", "source": "pizza", "source-layer": "places",
     "filter": ["!=", ["get", "style"], "neapolitan"],
     "layout": {"visibility": "none"}, "paint": {"circle-radius": 4, "circle-color": "#808080"}},
    {"id": "names", "type": "symbol", "source": "pizza", "source-layer": "places", "minzoom": 10, "maxzoom": 20,
     "layout": {"text-field": ["get", "name"], "text-size": ["step", ["zoom"], 10, 14, 14]},
     "paint": {"text-color": "#202020", "text-halo-color": "#ffffff", "text-halo-width": 1},
     "interactive": true}
  ]
})j";

TEST(FeatureStyleRules, APlainStyleJsonReadsAsRulesAndSurvivesASave) {
  FeatureStyle style;
  ASSERT_TRUE(fv::ReadFeatureStyleJson(kHandStyle, &style).ok());
  ASSERT_EQ(style.renderer, FeatureRenderer::kRules);
  ASSERT_EQ(style.rules.size(), 5u);
  EXPECT_EQ(style.label.field, "name");

  const fv::StyleRule& areas = style.rules[0];
  EXPECT_EQ(areas.id, "areas");
  EXPECT_EQ(areas.type, "fill");
  EXPECT_EQ(areas.filter, R"(["==",["geometry-type"],"Polygon"])");
  EXPECT_EQ(areas.paint, R"({"fill-color":"#cce0ff","fill-opacity":0.5})");
  const fv::StyleRule& roads = style.rules[1];
  EXPECT_EQ(roads.min_zoom, 8);
  EXPECT_EQ(roads.max_zoom, 24);
  EXPECT_EQ(roads.paint, R"({"line-color":"#888888","line-width":["interpolate",["linear"],["zoom"],8,1,16,6]})");
  const fv::StyleRule& naples = style.rules[2];
  EXPECT_EQ(naples.filter, R"(["all",["==",["get","style"],"neapolitan"],["has","name"]])");
  EXPECT_EQ(naples.legend, "Neapolitan");
  EXPECT_EQ(naples.extra, R"({"metadata":{"note":"kept"}})");
  EXPECT_FALSE(style.rules[3].shown);
  EXPECT_EQ(style.rules[3].layout, "{}");
  const fv::StyleRule& names = style.rules[4];
  EXPECT_EQ(names.type, "symbol");
  EXPECT_EQ(names.min_zoom, 10);
  EXPECT_EQ(names.max_zoom, 20);
  EXPECT_EQ(names.layout, R"({"text-field":["get","name"],"text-size":["step",["zoom"],10,14,14]})");
  EXPECT_EQ(names.extra, R"({"interactive":true})");

  std::string json;
  ASSERT_TRUE(fv::WriteFeatureStyleJson(style, &json).ok());
  EXPECT_NE(json.find("\"source-layer\": \"features\""), std::string::npos);
  FeatureStyle back;
  ASSERT_TRUE(fv::ReadFeatureStyleJson(json, &back).ok());
  EXPECT_TRUE(back == style);

  // Saved in a file and reopened, the rules come back unchanged.
  fv::FeatureOverlay overlay;
  overlay.SetLayer(Pizza());
  overlay.SetStyle(style);
  const std::string gpkg = Temp("pizza.gpkg");
  ASSERT_TRUE(overlay.FileSaveAs(gpkg, fv::FeatureOverlay::kFormatGeoPackage).ok());
  fv::FeatureOverlay reopened;
  ASSERT_TRUE(reopened.FileOpen(gpkg).ok());
  EXPECT_TRUE(reopened.style() == style);
}

TEST(FeatureStyleRules, RejectsWhatTheEngineCannotDraw) {
  FeatureStyle style;
  auto read = [&](const std::string& layers) {
    return fv::ReadFeatureStyleJson(R"({"version": 8, "metadata": {"fv:renderer": "rules"}, "layers": )" + layers + "}",
                                    &style)
        .code;
  };
  EXPECT_EQ(read(R"([{"id": "a", "type": "line"}])"), fv::kOk);
  EXPECT_EQ(read(R"([{"id": "a", "type": "heatmap"}])"), fv::kInvalidArg);
  EXPECT_EQ(read(R"([{"id": "a", "type": "line"}, {"id": "a", "type": "fill"}])"), fv::kInvalidArg);
  EXPECT_EQ(read(R"([{"type": "line"}])"), fv::kInvalidArg);
  EXPECT_EQ(read(R"([{"id": "a", "type": "line", "paint": {"line-width": ["frobnicate", 2]}}])"), fv::kInvalidArg);
  EXPECT_EQ(read(R"([{"id": "a", "type": "line", "filter": ["frobnicate"]}])"), fv::kInvalidArg);
  // Expressions only: no legacy filters or zoom functions.
  EXPECT_EQ(read(R"([{"id": "a", "type": "line", "filter": ["==", "class", "river"]}])"), fv::kInvalidArg);
  EXPECT_EQ(read(R"([{"id": "a", "type": "line", "filter": ["==", "$type", "Polygon"]}])"), fv::kInvalidArg);
  EXPECT_EQ(read(R"([{"id": "a", "type": "line", "paint": {"line-width": {"stops": [[8, 1], [16, 6]]}}}])"),
            fv::kInvalidArg);

  // Written rules are checked the same way.
  FeatureStyle rules;
  rules.renderer = FeatureRenderer::kRules;
  rules.rules.resize(2);
  rules.rules[0].id = rules.rules[1].id = "same";
  std::string json;
  EXPECT_EQ(fv::WriteFeatureStyleJson(rules, &json).code, fv::kInvalidArg);
  rules.rules[1].id = "other";
  rules.rules[1].paint = "[1]";
  EXPECT_EQ(fv::WriteFeatureStyleJson(rules, &json).code, fv::kInvalidArg);
  rules.rules[1].paint = "{}";
  rules.rules[1].filter = R"(["has", "name"])";
  EXPECT_EQ(fv::WriteFeatureStyleJson(rules, &json).code, fv::kOk);
  rules.rules[1].filter = R"(["!has", "name"])";
  EXPECT_EQ(fv::WriteFeatureStyleJson(rules, &json).code, fv::kInvalidArg);
}

TEST(FeatureStyleRules, AGeneratedStyleConvertsToRulesThatDrawTheSame) {
  auto pizza = Pizza();
  const FeatureStyle categorised = PizzaStyle(*pizza);
  FeatureStyle rules;
  ASSERT_TRUE(fv::FeatureStyleToRules(categorised, &rules).ok());
  ASSERT_EQ(rules.renderer, FeatureRenderer::kRules);
  std::vector<std::string> names;
  for (const fv::StyleRule& r : rules.rules) names.push_back(r.id);
  EXPECT_EQ(names, (std::vector<std::string>{"Areas", "Area outlines", "Line casings", "Lines", "Points",
                                             "Point labels", "Labels"}));
  EXPECT_EQ(rules.rules[1].facet, "fv-area-outline");
  EXPECT_EQ(rules.rules[5].facet, "");
  EXPECT_EQ(rules.label.field, "name");
  EXPECT_EQ(rules.overrides.size(), 1u);

  auto draw = [&](const FeatureStyle& style) {
    fv::FeatureOverlay overlay;
    overlay.SetLayer(pizza);
    overlay.SetStyle(style);
    fv::MapProjection proj;
    proj.SetSurfaceSize(300, 300);
    proj.SetCenter({32.78, -79.94});
    proj.SetScale(250000.0);
    auto canvas = std::make_unique<fv::CpuCanvas>(300, 300);
    canvas->Clear({255, 255, 255, 255});
    EXPECT_TRUE(overlay.OnDraw(proj, *canvas).ok());
    return canvas;
  };
  const auto a = draw(categorised);
  const auto b = draw(rules);
  int differ = 0, inked = 0;
  for (int y = 0; y < 300; ++y)
    for (int x = 0; x < 300 * 4; ++x) {
      differ += a->Buffer().Row(y)[x] != b->Buffer().Row(y)[x];
      inked += a->Buffer().Row(y)[x] != 255;
    }
  EXPECT_GT(inked, 100);
  EXPECT_EQ(differ, 0);

  // The override and the facets survive a save.
  std::string json;
  ASSERT_TRUE(fv::WriteFeatureStyleJson(rules, &json).ok());
  FeatureStyle back;
  ASSERT_TRUE(fv::ReadFeatureStyleJson(json, &back).ok());
  EXPECT_TRUE(back == rules);
  EXPECT_TRUE(back.overrides.at(1) == categorised.overrides.at(1));

  FeatureStyle again;
  ASSERT_TRUE(fv::FeatureStyleToRules(rules, &again).ok());
  EXPECT_TRUE(again == rules);
}

namespace {

/// Draws `style` over `layer` around Charleston, 300 px square, on white.
std::unique_ptr<fv::CpuCanvas> DrawPizza(const std::shared_ptr<fv::MemoryFeatureLayer>& layer,
                                         const FeatureStyle& style) {
  fv::FeatureOverlay overlay;
  overlay.SetLayer(layer);
  overlay.SetStyle(style);
  fv::MapProjection proj;
  proj.SetSurfaceSize(300, 300);
  proj.SetCenter({32.78, -79.94});
  proj.SetScale(250000.0);
  auto canvas = std::make_unique<fv::CpuCanvas>(300, 300);
  canvas->Clear({255, 255, 255, 255});
  EXPECT_TRUE(overlay.OnDraw(proj, *canvas).ok());
  return canvas;
}

int Differing(const fv::CpuCanvas& a, const fv::CpuCanvas& b) {
  int differ = 0;
  for (int y = 0; y < a.Buffer().Height(); ++y)
    for (int x = 0; x < a.Buffer().Width() * 4; ++x) differ += a.Buffer().Row(y)[x] != b.Buffer().Row(y)[x];
  return differ;
}

}  // namespace

TEST(FeatureStyleRules, CategoriesSplitIntoOneRulePerValueThatDrawTheSame) {
  auto pizza = Pizza();
  const FeatureStyle categorised = PizzaStyle(*pizza);
  FeatureStyle split;
  ASSERT_TRUE(fv::CategoriesToRules(categorised, &split, fv::GeometryType::kPoint).ok());
  ASSERT_EQ(split.renderer, FeatureRenderer::kRules);
  std::vector<std::string> names, legends;
  for (const fv::StyleRule& r : split.rules) {
    names.push_back(r.id);
    legends.push_back(r.legend);
  }
  EXPECT_EQ(names, (std::vector<std::string>{"Points: Other", "Points: Detroit", "Points: New York",
                                             "Points: Neapolitan", "Point labels"}));
  EXPECT_EQ(legends, (std::vector<std::string>{"Other", "Detroit", "New York", "Neapolitan", ""}));
  EXPECT_EQ(split.rules[3].filter,
            R"(["all",["match",["geometry-type"],["Point","MultiPoint"],true,false],)"
            R"(["==",["to-string",["get","style"]],"neapolitan"]])");
  EXPECT_EQ(split.rules[0].filter,
            R"(["all",["match",["geometry-type"],["Point","MultiPoint"],true,false],)"
            R"(["match",["to-string",["get","style"]],["neapolitan","new-york","detroit"],false,true]])");
  EXPECT_EQ(split.rules[3].facet, "fv-circle");
  EXPECT_EQ(split.overrides.size(), 1u);

  // The points do not overlap, so the drawing, override included, is unchanged.
  const auto a = DrawPizza(pizza, categorised);
  const auto b = DrawPizza(pizza, split);
  EXPECT_EQ(Differing(*a, *b), 0);

  std::vector<std::string> titles;
  for (const fv::RuleLegendEntry& e : fv::RuleLegend(split)) titles.push_back(e.title);
  EXPECT_EQ(titles, (std::vector<std::string>{"Point labels", "Neapolitan", "New York", "Detroit", "Other"}));

  FeatureStyle single;
  EXPECT_EQ(fv::CategoriesToRules(single, &split).code, fv::kInvalidArg);
}

TEST(FeatureStyleRules, TheLegendListsShownRulesTopFirstAndMergesSharedTitles) {
  FeatureStyle style;
  style.renderer = FeatureRenderer::kRules;
  style.rules.resize(4);
  style.rules[0].id = "casing";
  style.rules[0].legend = "Roads";
  style.rules[1].id = "rail";
  style.rules[2].id = "road";
  style.rules[2].legend = "Roads";
  style.rules[3].id = "hidden";
  style.rules[3].shown = false;
  const std::vector<fv::RuleLegendEntry> legend = fv::RuleLegend(style);
  ASSERT_EQ(legend.size(), 2u);
  EXPECT_EQ(legend[0].title, "Roads");
  EXPECT_EQ(legend[0].rules, (std::vector<size_t>{0, 2}));
  EXPECT_EQ(legend[1].title, "rail");
  EXPECT_EQ(legend[1].rules, (std::vector<size_t>{1}));
  EXPECT_TRUE(fv::RuleLegend(FeatureStyle()).empty());

  // A swatch draws a filtered rule, and a label rule as sample text.
  fv::CpuCanvas canvas(40, 20);
  canvas.Clear({255, 255, 255, 255});
  fv::StyleRule fill;
  fill.id = "a";
  fill.type = "fill";
  fill.filter = R"(["==",["get","kind"],"park"])";
  fill.paint = R"({"fill-color":"#00ff00"})";
  ASSERT_TRUE(fv::DrawRulesSwatch(canvas, {fill}, fv::GeometryType::kGeometry).ok());
  EXPECT_EQ(canvas.Buffer().Row(10)[20 * 4 + 1], 255);
  EXPECT_EQ(canvas.Buffer().Row(10)[20 * 4 + 0], 0);
  fv::CpuCanvas text(40, 20);
  text.Clear({255, 255, 255, 255});
  fv::StyleRule label;
  label.id = "b";
  label.type = "symbol";
  label.layout = R"({"text-field":["get","name"],"text-size":14})";
  std::string font;
  for (const char* f : {"/System/Library/Fonts/Supplemental/Arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"})
    if (fs::exists(f)) font = f;
  if (font.empty()) GTEST_SKIP() << "no system font to draw a label swatch";
  ASSERT_TRUE(fv::DrawRulesSwatch(text, {label}, fv::GeometryType::kPoint, 1.0, font).ok());
  int inked = 0;
  for (int y = 0; y < 20; ++y)
    for (int x = 0; x < 40 * 4; ++x) inked += text.Buffer().Row(y)[x] != 255;
  EXPECT_GT(inked, 10);
}

TEST(FeatureStyleRules, AnExportedStyleIsPlainMapLibreAndReadsBackWithoutOverrides) {
  auto pizza = Pizza();
  FeatureStyle categorised = PizzaStyle(*pizza);
  categorised.categories[1].symbol.marker = fv::FeatureMarker::kSquare;
  FeatureStyle split;
  ASSERT_TRUE(fv::CategoriesToRules(categorised, &split, fv::GeometryType::kPoint).ok());
  for (const FeatureStyle* style : {&categorised, &split}) {
    std::string json;
    ASSERT_TRUE(fv::ExportFeatureStyleJson(*style, &json).ok());
    const nlohmann::json doc = nlohmann::json::parse(json);
    int moved = 0;
    for (const nlohmann::json& l : doc["layers"]) {
      for (const char* section : {"paint", "layout"}) {
        // Named, not inline: items() would outlive a temporary from value() before C++23.
        const nlohmann::json props = l.value(section, nlohmann::json::object());
        for (const auto& p : props.items())
          EXPECT_NE(p.key().rfind("fv-", 0), 0u) << l["id"] << " " << p.key();
      }
      moved += l.value("metadata", nlohmann::json::object()).contains("fv:paint");
    }
    EXPECT_GT(moved, 0);
    EXPECT_EQ(json.find("fv:override"), std::string::npos);
    EXPECT_EQ(doc.value("glyphs", ""), "https://demotiles.maplibre.org/font/{fontstack}/{range}.pbf");
    FeatureStyle back;
    ASSERT_TRUE(fv::ImportFeatureStyleJson(json, &back).ok());
    FeatureStyle expected = *style;
    expected.overrides.clear();
    if (style == &categorised) {
      // The generated reader takes the "Other" symbol and labels from the document as written.
      EXPECT_EQ(back.renderer, FeatureRenderer::kCategorised);
      EXPECT_TRUE(back.categories == expected.categories);
      EXPECT_TRUE(back.symbol == expected.symbol);
    } else {
      EXPECT_TRUE(back == expected);
    }
  }
}

TEST(FeatureStyleRules, ImportRefusesWhatIsNotAStyle) {
  FeatureStyle style;
  EXPECT_EQ(fv::ImportFeatureStyleJson(R"({"type": "FeatureCollection", "features": []})", &style).code,
            fv::kInvalidArg);
  EXPECT_EQ(fv::ImportFeatureStyleJson(R"({"version": 7, "layers": []})", &style).code, fv::kInvalidArg);
  EXPECT_EQ(fv::ImportFeatureStyleJson("not json", &style).code, fv::kInvalidArg);
  ASSERT_TRUE(fv::ImportFeatureStyleJson(R"({"version": 8, "layers": [{"id": "a", "type": "line"}]})", &style).ok());
  EXPECT_EQ(style.renderer, FeatureRenderer::kRules);
}

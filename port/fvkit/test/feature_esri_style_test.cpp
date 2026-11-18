// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// EsriStyleFromDrawingInfo (port/web-features-plan.md WF4) against the NWS
/// watches/warnings layer document and two ArcGIS Online `drawingInfo`
/// objects recorded on 2026-11-17 (test/data/web/esri/style): the power plant
/// hexagon bins (classBreaks, transparency 25) and Hospitals (a picture marker).

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "fvkit/feature/web/esri.h"
#include "fvkit/feature/web/esri_style.h"
#include "web_fake_http.h"

namespace fv {
namespace web {
namespace {

std::string Fixture(const std::string& name) {
  std::ifstream in(std::string(FV_WEB_FIXTURES) + "/esri/" + name, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  EXPECT_FALSE(ss.str().empty()) << name;
  return ss.str();
}

bool Same(const FvColor& a, const FvColor& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

FeatureSchema Schema(std::vector<std::pair<std::string, FieldType>> fields) {
  FeatureSchema schema;
  for (auto& f : fields) {
    FieldDef d;
    d.name = f.first;
    d.type = f.second;
    schema.fields.push_back(d);
  }
  return schema;
}

Feature With(FieldValue value) {
  Feature f;
  f.id = 1;
  f.values.push_back(std::move(value));
  return f;
}

bool Mentions(const std::vector<std::string>& notes, const std::string& word) {
  for (const std::string& n : notes)
    if (n.find(word) != std::string::npos) return true;
  return false;
}

TEST(EsriStyle, TheNwsUniqueValueRendererBecomesCategorisedWithItsColours) {
  const std::string url =
      "https://mapservices.weather.noaa.gov/eventdriven/rest/services/WWA/watch_warn_adv/FeatureServer";
  auto http = std::make_shared<FakeHttp>();
  http->Route(url + "?f=json", Fixture("nws/service.json"));
  http->Route(url + "/1?f=json", Fixture("nws/layer1.json"));
  std::shared_ptr<EsriFeatureService> service;
  ASSERT_TRUE(EsriFeatureService::Open(http, url, {}, &service).ok());
  RemoteLayerInfo info;
  ASSERT_TRUE(service->Describe("1", &info, nullptr).ok());
  std::string drawing_info;
  ASSERT_TRUE(service->DrawingInfo("1", &drawing_info).ok());

  FeatureStyle style;
  std::vector<std::string> notes;
  ASSERT_TRUE(EsriStyleFromDrawingInfo(drawing_info, info.schema, &style, &notes).ok());
  EXPECT_TRUE(notes.empty()) << notes[0];
  EXPECT_EQ(style.renderer, FeatureRenderer::kCategorised);
  EXPECT_EQ(style.field, "prod_type");
  EXPECT_EQ(style.categories.size(), 111u);
  EXPECT_EQ(style.label.field, "");

  const FeatureSymbol& tornado = style.SymbolFor(With(std::string("Tornado Warning")), 0);
  EXPECT_TRUE(Same(tornado.area_fill, FvColor{255, 0, 0, 255}));
  EXPECT_TRUE(Same(tornado.area_outline, FvColor{110, 110, 110, 255}));
  EXPECT_NEAR(tornado.area_outline_px, 0.4 * 96 / 72, 1e-9);
  EXPECT_TRUE(Same(style.SymbolFor(With(std::string("Hurricane Warning")), 0).area_fill,
                   FvColor{220, 20, 60, 255}));
  // No default symbol: a value the renderer does not list is not drawn.
  EXPECT_EQ(style.SymbolFor(With(std::string("Unlisted")), 0).area_fill.a, 0);
  EXPECT_EQ(style.SymbolFor(With(std::string("Unlisted")), 0).area_outline_px, 0);

  std::string json;
  EXPECT_TRUE(WriteFeatureStyleJson(style, &json).ok());
}

TEST(EsriStyle, ClassBreaksBecomeGraduatedWithEsriClassBounds) {
  FeatureSchema schema = Schema({{"Point_Count", FieldType::kInteger}});
  FeatureStyle style;
  std::vector<std::string> notes;
  ASSERT_TRUE(EsriStyleFromDrawingInfo(Fixture("style/power_plant_hexbins_drawing_info.json"), schema, &style,
                                       &notes).ok());
  EXPECT_TRUE(notes.empty());
  ASSERT_EQ(style.renderer, FeatureRenderer::kGraduated);
  EXPECT_EQ(style.field, "Point_Count");
  ASSERT_EQ(style.classes.size(), 5u);
  const double inf = std::numeric_limits<double>::infinity();
  EXPECT_DOUBLE_EQ(style.classes[0].lower, 0.99999999);
  EXPECT_EQ(style.classes[1].lower, std::nextafter(4.0, inf));
  EXPECT_EQ(style.classes[2].lower, std::nextafter(10.0, inf));
  EXPECT_EQ(style.classes[3].lower, std::nextafter(21.0, inf));
  EXPECT_EQ(style.classes[4].lower, std::nextafter(40.0, inf));
  EXPECT_EQ(style.classes[1].label, "5 - 10");
  // Esri classes include their maximum: a count of 4 is in the first class.
  EXPECT_EQ(&style.SymbolFor(With(int64_t{4}), 0), &style.classes[0].symbol);
  EXPECT_EQ(&style.SymbolFor(With(int64_t{5}), 0), &style.classes[1].symbol);
  // Transparency 25 leaves three quarters of each alpha.
  EXPECT_TRUE(Same(style.classes[0].symbol.area_fill, FvColor{246, 229, 207, 191}));
  EXPECT_TRUE(Same(style.classes[0].symbol.area_outline, FvColor{255, 251, 253, 191}));
  EXPECT_NEAR(style.classes[0].symbol.area_outline_px, 96.0 / 72.0, 1e-9);
}

TEST(EsriStyle, APictureMarkerFallsBackToTheDefaultSymbolAndSaysSo) {
  FeatureStyle style;
  std::vector<std::string> notes;
  ASSERT_TRUE(EsriStyleFromDrawingInfo(Fixture("style/hospitals_drawing_info.json"), FeatureSchema(), &style,
                                       &notes).ok());
  EXPECT_EQ(style.renderer, FeatureRenderer::kSingle);
  EXPECT_EQ(style.symbol, FeatureSymbol());
  ASSERT_EQ(notes.size(), 1u);
  EXPECT_TRUE(Mentions(notes, "Picture symbols"));
}

TEST(EsriStyle, SimpleSymbolsAndLabelsAreReadInPixels) {
  FeatureSchema schema = Schema({{"NAME", FieldType::kText}, {"class", FieldType::kInteger}});
  FeatureStyle style;
  std::vector<std::string> notes;
  ASSERT_TRUE(EsriStyleFromDrawingInfo(R"({"renderer":{"type":"simple","symbol":
      {"type":"esriSMS","style":"esriSMSSquare","color":[10,20,30,255],"size":6,
       "outline":{"color":[0,0,0,128],"width":1.5}}},
      "labelingInfo":[{"labelExpressionInfo":{"expression":"$feature[\"name\"]"},
        "symbol":{"type":"esriTS","color":[0,0,255,255],"haloColor":[255,255,0,255],"haloSize":1.5,
                  "font":{"family":"Arial","size":9}}},
        {"labelExpression":"[class]"}]})",
                                       schema, &style, &notes).ok());
  EXPECT_EQ(style.symbol.marker, FeatureMarker::kSquare);
  EXPECT_DOUBLE_EQ(style.symbol.marker_size_px, 8.0);
  EXPECT_TRUE(Same(style.symbol.marker_fill, FvColor{10, 20, 30, 255}));
  EXPECT_TRUE(Same(style.symbol.marker_outline, FvColor{0, 0, 0, 128}));
  EXPECT_DOUBLE_EQ(style.symbol.marker_outline_px, 2.0);
  EXPECT_EQ(style.label.field, "NAME");  // the schema's spelling
  EXPECT_DOUBLE_EQ(style.label.size_px, 12.0);
  EXPECT_TRUE(Same(style.label.color, FvColor{0, 0, 255, 255}));
  EXPECT_TRUE(Same(style.label.halo_color, FvColor{255, 255, 0, 255}));
  EXPECT_DOUBLE_EQ(style.label.halo_px, 2.0);
  ASSERT_EQ(notes.size(), 1u);
  EXPECT_TRUE(Mentions(notes, "first of the layer's label classes"));

  ASSERT_TRUE(EsriStyleFromDrawingInfo(R"({"renderer":{"type":"simple","symbol":
      {"type":"esriSLS","style":"esriSLSDashDotDot","color":[1,2,3,255],"width":3}},
      "labelingInfo":[{"labelExpression":"[class]"}]})",
                                       schema, &style, &notes).ok());
  EXPECT_EQ(style.symbol.dash, "dash-dot");
  EXPECT_DOUBLE_EQ(style.symbol.line_width_px, 4.0);
  EXPECT_EQ(style.label.field, "class");
  EXPECT_TRUE(notes.empty());
}

TEST(EsriStyle, NumericCategoriesAreKeyedAsTheLayerKeysThem) {
  FeatureSchema schema = Schema({{"depth", FieldType::kReal}});
  FeatureStyle style;
  ASSERT_TRUE(EsriStyleFromDrawingInfo(R"({"renderer":{"type":"uniqueValue","field1":"depth",
      "defaultSymbol":{"type":"esriSFS","style":"esriSFSNull","outline":{"color":[9,9,9,255],"width":1}},
      "uniqueValueInfos":[{"value":"1.50","symbol":{"type":"esriSFS","style":"esriSFSSolid","color":[1,1,1,255]}},
                          {"value":"1.5","symbol":{"type":"esriSFS","style":"esriSFSSolid","color":[2,2,2,255]}}]}})",
                                       schema, &style).ok());
  ASSERT_EQ(style.categories.size(), 1u);  // the second is the same value
  EXPECT_EQ(style.categories[0].value, "1.5");
  EXPECT_EQ(style.SymbolFor(With(1.5), 0).area_fill.r, 1);
  EXPECT_EQ(style.symbol.area_fill.a, 0);  // esriSFSNull: outline only
  EXPECT_EQ(style.symbol.area_outline.r, 9);
}

TEST(EsriStyle, WhatCannotBeImportedFallsBackAndIsNoted) {
  FeatureSchema schema = Schema({{"a", FieldType::kText}, {"b", FieldType::kText}, {"n", FieldType::kReal}});
  FeatureStyle style;
  std::vector<std::string> notes;
  ASSERT_TRUE(EsriStyleFromDrawingInfo(R"({"renderer":{"type":"uniqueValue","field1":"a","field2":"b",
      "defaultSymbol":{"type":"esriSLS","color":[5,5,5,255],"width":1},"uniqueValueInfos":[]}})",
                                       schema, &style, &notes).ok());
  EXPECT_EQ(style.renderer, FeatureRenderer::kSingle);
  EXPECT_EQ(style.symbol.line_color.r, 5);
  EXPECT_TRUE(Mentions(notes, "several fields"));

  ASSERT_TRUE(EsriStyleFromDrawingInfo(R"({"renderer":{"type":"classBreaks","field":"n",
      "normalizationType":"esriNormalizeByField","normalizationField":"a","minValue":0,
      "classBreakInfos":[{"classMaxValue":1,"symbol":{"type":"esriSFS","color":[1,1,1,255]}}]}})",
                                       schema, &style, &notes).ok());
  EXPECT_EQ(style.renderer, FeatureRenderer::kSingle);
  EXPECT_TRUE(Mentions(notes, "Normalised"));

  ASSERT_TRUE(EsriStyleFromDrawingInfo(R"({"renderer":{"type":"heatmap","colorStops":[]}})", schema, &style,
                                       &notes).ok());
  EXPECT_EQ(style, FeatureStyle());
  EXPECT_TRUE(Mentions(notes, "heatmap"));

  ASSERT_TRUE(EsriStyleFromDrawingInfo(R"({"renderer":{"type":"simple",
      "symbol":{"type":"esriSMS","style":"esriSMSDiamond","color":[1,1,1,255],"size":4},
      "visualVariables":[{"type":"sizeInfo","field":"n"}]},
      "labelingInfo":[{"labelExpressionInfo":{"expression":"$feature.a + ' ' + $feature.b"}}]})",
                                       schema, &style, &notes).ok());
  EXPECT_TRUE(Mentions(notes, "Visual variables"));
  EXPECT_TRUE(Mentions(notes, "drawn as a circle"));
  EXPECT_TRUE(Mentions(notes, "not a single field"));
  EXPECT_EQ(style.label.field, "");

  EXPECT_EQ(EsriStyleFromDrawingInfo("[1]", schema, &style).code, kInvalidArg);
}

}  // namespace
}  // namespace web
}  // namespace fv

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for the `peregrine:legend` style metadata and the swatch renderer.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/geo/web_mercator.h"
#include "fvkit/style/legend.h"
#include "fvkit/style/maplibre_style.h"

namespace fv {
namespace {

/// A two-layer road (red casing from z12, white fill from z13) on a grey
/// ground, with `legend` spliced into its metadata.
std::string Style(const std::string& legend) {
  return R"({"version": 8, "name": "t",
    "metadata": {"peregrine:legend": )" + legend + R"(},
    "layers": [
      {"id": "bg", "type": "background", "paint": {"background-color": "#808080"}},
      {"id": "road-casing", "type": "line", "source-layer": "roads", "minzoom": 12,
       "filter": ["==", "class", "minor"],
       "paint": {"line-color": "#ff0000", "line-width": 6}},
      {"id": "road", "type": "line", "source-layer": "roads", "minzoom": 13,
       "filter": ["==", "class", "minor"],
       "paint": {"line-color": "#ffffff", "line-width": 2}},
      {"id": "road-arrow", "type": "symbol", "source-layer": "roads",
       "filter": ["==", "class", "minor"], "layout": {"text-field": "x"}}
    ]})";
}

const char kGoodLegend[] = R"({
  "groups": [
    {"title": "Roads", "subtitle": "The edge",
     "entries": [
       {"id": "quiet", "label": "Quiet street", "detail": "Red edge.",
        "specimen": [{"layer": "roads", "tags": {"class": "minor", "maxspeed": 40}}],
        "osm": {"highway": "residential"}},
       {"id": "stop", "label": "Stop", "specimen": [{"layer": "roads", "geometry": "point"}]}
     ]}],
  "unlisted_layers": ["road-arr*"]})";

TEST(StyleLegend, GoodLegendRoundTrips) {
  MapLibreStyleEngine e;
  std::string err;
  ASSERT_TRUE(e.LoadText(Style(kGoodLegend), &err).ok()) << err;
  EXPECT_EQ(e.legend_error(), "");
  ASSERT_EQ(e.legend().size(), 1u);
  const MapLibreLegendGroup& g = e.legend()[0];
  EXPECT_EQ(g.title, "Roads");
  EXPECT_EQ(g.subtitle, "The edge");
  ASSERT_EQ(g.entries.size(), 2u);
  const MapLibreLegendEntry& q = g.entries[0];
  EXPECT_EQ(q.id, "quiet");
  EXPECT_EQ(q.label, "Quiet street");
  EXPECT_EQ(q.detail, "Red edge.");
  ASSERT_EQ(q.specimen.size(), 1u);
  EXPECT_EQ(q.specimen[0].layer, "roads");
  EXPECT_EQ(q.specimen[0].type, VectorGeometryType::kLine);
  const std::vector<std::pair<std::string, std::string>> tags = {
      {"class", "minor"}, {"maxspeed", "40"}};
  EXPECT_EQ(q.specimen[0].tags, tags);
  ASSERT_EQ(q.osm.size(), 1u);
  EXPECT_EQ(q.osm[0].second, "residential");
  EXPECT_EQ(g.entries[1].specimen[0].type, VectorGeometryType::kPoint);
  EXPECT_EQ(e.legend_unlisted_layers(), std::vector<std::string>{"road-arr*"});
}

TEST(StyleLegend, StyleWithoutLegendHasNoError) {
  MapLibreStyleEngine e;
  ASSERT_TRUE(e.LoadText(R"({"version": 8, "layers": []})").ok());
  EXPECT_TRUE(e.legend().empty());
  EXPECT_EQ(e.legend_error(), "");
}

// Each malformed legend leaves the map loaded, the legend empty, and an error
// that names the problem.
TEST(StyleLegend, MalformedLegendLeavesTheStyleLoaded) {
  const struct { const char* legend; const char* says; } kCases[] = {
      {R"([])", "not an object"},
      {R"({})", "no 'groups'"},
      {R"({"groups": [], "colour": 1})", "unknown key 'colour'"},
      {R"({"groups": [{"entries": []}]})", "no 'title'"},
      {R"({"groups": [{"title": "R", "entries": [{"id": "a", "label": "A",
           "specimen": [{"layer": "roads"}], "lable": "x"}]}]})",
       "unknown key 'lable'"},
      {R"({"groups": [{"title": "R", "entries": [{"id": "a", "label": "A",
           "specimen": []}]}]})",
       "no 'specimen'"},
      {R"({"groups": [{"title": "R", "entries": [{"id": "a", "label": "A",
           "specimen": [{"layer": "rodas"}]}]}]})",
       "source layer 'rodas'"},
      {R"({"groups": [{"title": "R", "entries": [{"id": "a", "label": "A",
           "specimen": [{"layer": "roads", "geometry": "polygon"}]}]}]})",
       "geometry"},
      {R"({"groups": [{"title": "R", "entries": [{"id": "a", "label": "A",
           "specimen": [{"layer": "roads", "tags": {"class": ["minor"]}}]}]}]})",
       "tags.class"},
      {R"({"groups": [{"title": "R", "entries": [
           {"id": "a", "label": "A", "specimen": [{"layer": "roads"}]},
           {"id": "a", "label": "B", "specimen": [{"layer": "roads"}]}]}]})",
       "listed twice"},
      {R"({"groups": [], "unlisted_layers": ["rail*"]})", "'rail*' names no style layer"},
  };
  for (const auto& c : kCases) {
    MapLibreStyleEngine e;
    std::string err;
    ASSERT_TRUE(e.LoadText(Style(c.legend), &err).ok()) << c.legend << ": " << err;
    EXPECT_EQ(e.layers().size(), 4u) << c.legend;
    EXPECT_TRUE(e.legend().empty()) << c.legend;
    EXPECT_NE(e.legend_error().find(c.says), std::string::npos)
        << c.legend << " -> " << e.legend_error();
  }
}

/// Returns the RGB of pixel (x, y) as 0xRRGGBB.
uint32_t Rgb(const CpuCanvas& c, int x, int y) {
  const unsigned char* p = c.Buffer().Row(y) + x * 4;
  return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
}

// The swatch is drawn by the style at the map's scale: casing under fill, at
// their authored widths in points, on the style's background.
TEST(StyleLegend, SwatchIsTheStyleAtTheMapsScale) {
  MapLibreStyleEngine e;
  ASSERT_TRUE(e.LoadText(Style(kGoodLegend)).ok());
  const MapLibreLegendEntry& quiet = e.legend()[0].entries[0];
  const double ppp = 2.0;
  const int w_pt = 40, h_pt = 20;
  CpuCanvas canvas(int(w_pt * ppp), int(h_pt * ppp));

  const double z16 = webmerc::ScaleForZoomExact(16.0, e.reference_latitude(),
                                                e.display_mm_per_pixel());
  const std::vector<std::string> drew =
      RenderLegendSwatch(e, quiet, z16, ppp, w_pt, h_pt, &canvas);
  EXPECT_EQ(drew, (std::vector<std::string>{"road-casing", "road"}));
  EXPECT_EQ(Rgb(canvas, 0, 0), 0x808080u);
  EXPECT_EQ(Rgb(canvas, 40, 20), 0xffffffu) << "the fill draws over the casing";
  // A 6 pt casing at 2 px per point: 12 red-or-white rows down the middle.
  int painted = 0;
  for (int y = 0; y < h_pt * ppp; ++y) painted += Rgb(canvas, 40, y) != 0x808080u;
  EXPECT_NEAR(painted, 12, 1);
  EXPECT_EQ(Rgb(canvas, 40, 20 - 5), 0xff0000u);

  // Below the casing's minzoom nothing draws, and the swatch is bare ground.
  const double z11 = webmerc::ScaleForZoomExact(11.0, e.reference_latitude(),
                                                e.display_mm_per_pixel());
  EXPECT_TRUE(RenderLegendSwatch(e, quiet, z11, ppp, w_pt, h_pt, &canvas).empty());
  EXPECT_EQ(Rgb(canvas, 40, 20), 0x808080u);
  EXPECT_DOUBLE_EQ(LegendEntryMinZoom(e, quiet), 12.0);
}

// A specimen whose tags no filter matches draws nothing at any zoom.
TEST(StyleLegend, SpecimenThatMatchesNoFilterHasNoMinZoom) {
  MapLibreStyleEngine e;
  ASSERT_TRUE(e.LoadText(Style(kGoodLegend)).ok());
  MapLibreLegendEntry stray;
  stray.specimen.push_back({"roads", VectorGeometryType::kLine, {{"class", "major"}}});
  EXPECT_LT(LegendEntryMinZoom(e, stray), 0.0);
}

// The map engine names the layers that drew since the last reset, and a row
// is on the map when one of its swatch layers is among them.
TEST(StyleLegend, RowIsOnTheMapWhenOneOfItsLayersDrew) {
  MapLibreStyleEngine e;
  ASSERT_TRUE(e.LoadText(Style(kGoodLegend)).ok());
  EXPECT_TRUE(e.layer_ids_that_drew().empty());

  StyleContext ctx;
  ctx.scale_denominator = webmerc::ScaleForZoomExact(12.5, e.reference_latitude(),
                                                     e.display_mm_per_pixel());
  VectorFeature road;
  road.type = VectorGeometryType::kLine;
  road.layer = "roads";
  road.attributes = {{"class", "minor"}};
  road.parts = {{GeoPoint{0.0, 0.0}, GeoPoint{0.0, 0.001}}};
  std::vector<StyleResult> out;
  ASSERT_TRUE(e.Style(road, ctx, &out).ok());
  const std::vector<std::string> drew = e.layer_ids_that_drew();
  EXPECT_EQ(drew, std::vector<std::string>{"road-casing"})
      << "the fill starts at z13";
  EXPECT_EQ(e.layers_that_drew(), drew.size());

  const MapLibreLegendEntry& quiet = e.legend()[0].entries[0];
  const double z16 = webmerc::ScaleForZoomExact(16.0, e.reference_latitude(),
                                                e.display_mm_per_pixel());
  // At z16 the row is casing and fill; the map drew only the casing.
  const std::vector<std::string> row = LegendEntryLayers(e, quiet, z16);
  EXPECT_FALSE(LegendEntryOnMap(row, drew));
  EXPECT_TRUE(LegendEntryOnMap(row, {"road-casing", "road", "road-arrow"}));
  EXPECT_TRUE(LegendEntryOnMap(LegendEntryLayers(e, quiet, ctx.scale_denominator), drew));
  EXPECT_FALSE(LegendEntryOnMap({}, drew));

  e.ResetDiagnostics();
  EXPECT_TRUE(e.layer_ids_that_drew().empty());
}

}  // namespace
}  // namespace fv

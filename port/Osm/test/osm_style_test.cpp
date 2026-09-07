// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv::OsmStyleEngine tests (OSM phase O2).
//
// Three things are worth pinning here and the rest follows from them:
//
//   1. THE ZOOM<->SCALE RELATION round-trips, and it is the SAME one the
//      source uses. This is the load-bearing claim of the whole session: if a
//      style layer's minzoom lands on a different scale than the tile level
//      the source read, the map draws z12 geometry with z9 symbology and
//      nobody can tell by looking.
//   2. THE SUBSET IS ENFORCED. Every rejection below is a style a real
//      designer would hand us; the test exists so that "we fail loudly" is a
//      property of the code and not of the header comment.
//   3. DRAW ORDER IS STYLE-LAYER ORDER. The casing/fill pair is the case that
//      breaks visibly when it is not, so it is asserted on one feature that
//      matches both layers.

#include "fv_osm_style.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "fv_osm_vector_source.h"
#include "fvkit/tools/png_write.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/vector/renderer.h"
#include "fv_web_mercator.h"

namespace fs = std::filesystem;

namespace {

std::string StylePath() {
  const char* d = getenv("FVW_OSM_STYLE_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/peregrine-osm.json";
  return fs::is_regular_file(p) ? p : std::string();
}

std::string MbtilesPath() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/OSM/mbtiles/us-south.mbtiles";
  return fs::is_regular_file(p) ? p : std::string();
}

fv::VectorFeature Road(const char* klass) {
  fv::VectorFeature f;
  f.type = fv::VectorGeometryType::kLine;
  f.layer = "transportation";
  f.style_key = klass;
  f.attributes.push_back({"class", klass});
  f.parts.push_back({{33.75, -84.39}, {33.76, -84.38}});
  return f;
}

// A minimal well-formed style around whatever layer array the caller wants.
std::string Wrap(const std::string& layers) {
  return "{\"version\":8,\"name\":\"t\",\"layers\":[" + layers + "]}";
}

const fv::OsmStyleLayerInfo* Info(const fv::OsmStyleEngine& e,
                                  const std::string& id) {
  for (const auto& i : e.layers())
    if (i.id == id) return &i;
  return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. The one zoom<->scale relation
// ---------------------------------------------------------------------------

TEST(OsmZoomScale, RoundTripsExactly) {
  // Charleston-ish and equatorial, at the port's reference pixel pitch and at
  // a retina-ish one, so neither argument is silently ignored.
  for (double lat : {0.0, 32.78, -45.0}) {
    for (double mm : {0.25, 0.1}) {
      for (int z = 0; z <= 20; ++z) {
        const double denom = fv::webmerc::ScaleForZoomExact(z, lat, mm);
        ASSERT_TRUE(std::isfinite(denom)) << z;
        ASSERT_GT(denom, 0.0);
        const double back = fv::webmerc::ZoomForScaleExact(denom, lat, mm);
        EXPECT_NEAR(back, z, 1e-9) << "z=" << z << " lat=" << lat << " mm=" << mm;
      }
    }
  }
}

TEST(OsmZoomScale, AgreesWithTheSourcesClampedChoice) {
  // ZoomForScale (what O1's source calls) must be the rounded, clamped form of
  // the exact relation the style engine bands with — not a second derivation.
  const double lat = 33.75, mm = 0.25;
  for (int z = 2; z <= 14; ++z) {
    const double denom = fv::webmerc::ScaleForZoomExact(z, lat, mm);
    EXPECT_EQ(fv::webmerc::ZoomForScale(denom, lat, mm, 0, 14), z);
    // A scale a third of the way to the next level still rounds here.
    const double nudged = denom * std::pow(2.0, -0.3);
    EXPECT_EQ(fv::webmerc::ZoomForScale(nudged, lat, mm, 0, 14), z);
  }
}

TEST(OsmZoomScale, EngineDerivesTheSameZoomAsTheRelation) {
  fv::OsmStyleEngine e;
  ASSERT_TRUE(e.LoadText(Wrap("")).ok());
  e.SetReferenceLatitude(33.75);
  e.SetDisplayMmPerPixel(0.25);
  const double denom = fv::webmerc::ScaleForZoomExact(12, 33.75, 0.25);
  EXPECT_NEAR(e.ZoomForScale(denom), 12.0, 1e-9);

  // No scale at all: the declared scale-less zoom, not a NaN and not a guess.
  EXPECT_DOUBLE_EQ(e.ZoomForScale(0.0), 14.0);
  e.SetScalelessZoom(10.0);
  EXPECT_DOUBLE_EQ(e.ZoomForScale(0.0), 10.0);

  e.SetZoomOverride(7.5);
  EXPECT_DOUBLE_EQ(e.ZoomForScale(denom), 7.5);
  e.SetZoomOverride(-1.0);
  EXPECT_NEAR(e.ZoomForScale(denom), 12.0, 1e-9);
}

TEST(OsmStyleBands, MinZoomBecomesACoarseScaleBound) {
  fv::OsmStyleEngine e;
  e.SetReferenceLatitude(0.0);
  ASSERT_TRUE(e
                  .LoadText(Wrap(
                      R"({"id":"a","type":"line","source-layer":"transportation",
                          "minzoom":10,"maxzoom":14,
                          "paint":{"line-color":"#000","line-width":2}})"))
                  .ok());
  const fv::OsmStyleLayerInfo* i = Info(e, "a");
  ASSERT_NE(i, nullptr);
  // Zoom grows as scale gets finer, so minzoom is the LARGER denominator.
  EXPECT_NEAR(i->band.max_denom, fv::webmerc::ScaleForZoomExact(10, 0.0, 0.25),
              1e-6);
  EXPECT_NEAR(i->band.min_denom, fv::webmerc::ScaleForZoomExact(14, 0.0, 0.25),
              1e-6);
  EXPECT_GT(i->band.max_denom, i->band.min_denom);

  // And the band actually gates: z8 (coarser) out, z12 in, z16 (finer) out.
  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;
  ctx.scale_denominator = fv::webmerc::ScaleForZoomExact(8, 0.0, 0.25);
  ASSERT_TRUE(e.Style(Road("motorway"), ctx, &out).ok());
  EXPECT_TRUE(out.empty());

  out.clear();
  ctx.scale_denominator = fv::webmerc::ScaleForZoomExact(12, 0.0, 0.25);
  ASSERT_TRUE(e.Style(Road("motorway"), ctx, &out).ok());
  EXPECT_EQ(out.size(), 1u);

  out.clear();
  ctx.scale_denominator = fv::webmerc::ScaleForZoomExact(16, 0.0, 0.25);
  ASSERT_TRUE(e.Style(Road("motorway"), ctx, &out).ok());
  EXPECT_TRUE(out.empty());
}

TEST(OsmStyleBands, NoScaleThinsNothing) {
  // fvkit's convention: scale 0 means "the caller has no scale" and must not
  // lose features to thinning it did not ask for (rules.h, ScaleBand).
  fv::OsmStyleEngine e;
  ASSERT_TRUE(e
                  .LoadText(Wrap(
                      R"({"id":"a","type":"line","source-layer":"transportation",
                          "minzoom":18,"paint":{"line-color":"#000"}})"))
                  .ok());
  fv::StyleContext ctx;  // scale_denominator = 0
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e.Style(Road("motorway"), ctx, &out).ok());
  EXPECT_EQ(out.size(), 1u);
}

// ---------------------------------------------------------------------------
// 2. The declared subset, enforced
// ---------------------------------------------------------------------------

TEST(OsmStyleLoad, RejectsExpressionFilters) {
  fv::OsmStyleEngine e;
  std::string err;
  const fv::Status s = e.LoadText(
      Wrap(R"({"id":"roads","type":"line","source-layer":"transportation",
               "filter":["match",["get","class"],["motorway"],true,false],
               "paint":{"line-color":"#000"}})"),
      &err);
  EXPECT_EQ(s.code, fv::kUnsupported);
  EXPECT_NE(err.find("roads"), std::string::npos) << err;
  EXPECT_NE(err.find("match"), std::string::npos) << err;
}

TEST(OsmStyleLoad, RejectsInterpolateExpressions) {
  fv::OsmStyleEngine e;
  const fv::Status s = e.LoadText(
      Wrap(R"({"id":"roads","type":"line","source-layer":"transportation",
               "paint":{"line-color":"#000",
                        "line-width":["interpolate",["linear"],["zoom"],10,1,16,8]}})"));
  EXPECT_EQ(s.code, fv::kUnsupported);
  EXPECT_NE(s.message.find("line-width"), std::string::npos) << s.message;
}

TEST(OsmStyleLoad, RejectsDataDrivenFunctions) {
  fv::OsmStyleEngine e;
  const fv::Status s = e.LoadText(
      Wrap(R"({"id":"roads","type":"line","source-layer":"transportation",
               "paint":{"line-color":"#000",
                        "line-width":{"property":"lanes","stops":[[1,2],[4,8]]}}})"));
  EXPECT_EQ(s.code, fv::kUnsupported);
  EXPECT_NE(s.message.find("data-driven"), std::string::npos) << s.message;
}

TEST(OsmStyleLoad, RejectsUnsupportedLayerTypesAndExpressions) {
  fv::OsmStyleEngine e;
  EXPECT_EQ(e.LoadText(Wrap(R"({"id":"h","type":"hillshade","source-layer":"x"})"))
                .code,
            fv::kUnsupported);
  EXPECT_EQ(e.LoadText(Wrap(R"({"id":"t","type":"symbol","source-layer":"place",
                                "layout":{"text-field":["get","name"]}})"))
                .code,
            fv::kUnsupported);
  // A pattern NAME is a constant or a token template, never an expression —
  // the same rule every other property here follows.
  EXPECT_EQ(e.LoadText(Wrap(R"({"id":"b","type":"fill","source-layer":"building",
                                "paint":{"fill-pattern":["get","kind"]}})"))
                .code,
            fv::kUnsupported);
}

// `fill-pattern` used to be in the test above. O6 loads sprite sheets, so it
// is supported now and the load must SUCCEED — deliberately, which is why the
// change of behaviour gets an assertion of its own rather than a quiet
// deletion from the rejection list.
TEST(OsmStyleLoad, AFillPatternLoadsNowThatSheetsAreRead) {
  fv::OsmStyleEngine e;
  EXPECT_TRUE(e.LoadText(Wrap(
                   R"({"id":"b","type":"fill","source-layer":"building",
                       "paint":{"fill-pattern":"hatch"}})"))
                  .ok());
  // With no sheet behind it the layer simply draws nothing and says so.
  EXPECT_TRUE(e.sprite_ids().empty());
}

TEST(OsmStyleLoad, RejectsUnknownColourAndBadJson) {
  fv::OsmStyleEngine e;
  EXPECT_EQ(e.LoadText(Wrap(R"({"id":"w","type":"fill","source-layer":"water",
                                "paint":{"fill-color":"seafoam"}})"))
                .code,
            fv::kUnsupported);
  EXPECT_EQ(e.LoadText("{not json").code, fv::kInvalidArg);
  EXPECT_EQ(e.LoadText(Wrap(R"({"id":"v","type":"fill","source-layer":"water"})") +
                       R"(], "version": 7, "x":[)")
                .code,
            fv::kInvalidArg);
}

TEST(OsmStyleLoad, AFailedLoadLeavesThePreviousStyleIntact) {
  fv::OsmStyleEngine e;
  ASSERT_TRUE(e
                  .LoadText(Wrap(
                      R"({"id":"good","type":"line","source-layer":"transportation",
                          "paint":{"line-color":"#ff0000","line-width":3}})"))
                  .ok());
  ASSERT_EQ(e.layers().size(), 1u);
  EXPECT_FALSE(e.LoadText(Wrap(R"({"id":"bad","type":"heatmap","source-layer":"x"})"))
                   .ok());
  ASSERT_EQ(e.layers().size(), 1u);
  EXPECT_EQ(e.layers()[0].id, "good");
  EXPECT_TRUE(e.IsOpen());
}

// ---------------------------------------------------------------------------
// Colours, zoom functions, filters
// ---------------------------------------------------------------------------

TEST(OsmStyleColour, ParsesTheFormsAGlStyleUses) {
  fv::OsmStyleEngine e;
  ASSERT_TRUE(e
                  .LoadText(Wrap(
                      R"J({"id":"bg","type":"background",
                          "paint":{"background-color":"hsl(30, 50%, 90%)"}},
                         {"id":"w","type":"fill","source-layer":"water",
                          "paint":{"fill-color":"#a0c8f0","fill-opacity":0.5}},
                         {"id":"r","type":"line","source-layer":"transportation",
                          "paint":{"line-color":"rgba(255, 0, 0, 0.25)"}})J"))
                  .ok());
  fv::FvColor bg;
  ASSERT_TRUE(e.background(0.0, &bg));
  // hsl(30, 50%, 90%) -> #f2e6d9 (CSS: c=0.1, x=0.05, m=0.85).
  EXPECT_EQ(int(bg.r), 0xF2);
  EXPECT_EQ(int(bg.g), 0xE6);
  EXPECT_EQ(int(bg.b), 0xD9);
  EXPECT_EQ(int(bg.a), 255);

  fv::VectorFeature water;
  water.type = fv::VectorGeometryType::kArea;
  water.layer = "water";
  water.parts.push_back({{0, 0}, {0, 1}, {1, 1}});
  std::vector<fv::StyleResult> out;
  fv::StyleContext ctx;
  ASSERT_TRUE(e.Style(water, ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  ASSERT_TRUE(out[0].fill.valid);
  EXPECT_EQ(int(out[0].fill.brush.color.r), 0xA0);
  EXPECT_EQ(int(out[0].fill.brush.color.a), 128);  // 255 * 0.5

  out.clear();
  ASSERT_TRUE(e.Style(Road("motorway"), ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  ASSERT_TRUE(out[0].stroke.valid);
  EXPECT_EQ(int(out[0].stroke.pen.color.r), 255);
  EXPECT_EQ(int(out[0].stroke.pen.color.a), 64);  // 255 * 0.25
}

TEST(OsmStyleZoomFn, InterpolatesWidthsBetweenStops) {
  fv::OsmStyleEngine e;
  e.SetReferenceLatitude(0.0);
  ASSERT_TRUE(e
                  .LoadText(Wrap(
                      R"({"id":"r","type":"line","source-layer":"transportation",
                          "paint":{"line-color":"#000",
                                   "line-width":{"stops":[[10,2],[20,22]]}}})"))
                  .ok());
  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;
  auto width_at_zoom = [&](double z) {
    out.clear();
    ctx.scale_denominator = fv::webmerc::ScaleForZoomExact(z, 0.0, 0.25);
    EXPECT_TRUE(e.Style(Road("motorway"), ctx, &out).ok());
    return out.empty() ? -1 : out[0].stroke.pen.width;
  };
  EXPECT_EQ(width_at_zoom(10.0), 2);   // at the first stop
  EXPECT_EQ(width_at_zoom(15.0), 12);  // linear (base 1) halfway
  EXPECT_EQ(width_at_zoom(20.0), 22);  // at the last stop
  EXPECT_EQ(width_at_zoom(4.0), 2);    // clamped below
  EXPECT_EQ(width_at_zoom(23.0), 22);  // clamped above
}

TEST(OsmStyleZoomFn, HonoursDeviceDpi) {
  fv::OsmStyleEngine e;
  ASSERT_TRUE(e
                  .LoadText(Wrap(
                      R"({"id":"r","type":"line","source-layer":"transportation",
                          "paint":{"line-color":"#000","line-width":4}})"))
                  .ok());
  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e.Style(Road("motorway"), ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].stroke.pen.width, 4);  // 96 dpi = the style's own unit

  out.clear();
  ctx.device_dpi = 192.0;
  ASSERT_TRUE(e.Style(Road("motorway"), ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].stroke.pen.width, 8);
}

TEST(OsmStyleFilter, LegacyOperatorsIncludingMissingTagSemantics) {
  fv::OsmStyleEngine e;
  ASSERT_TRUE(e
                  .LoadText(Wrap(
                      R"({"id":"notbridge","type":"line","source-layer":"transportation",
                          "filter":["!=","brunnel","bridge"],
                          "paint":{"line-color":"#000"}},
                         {"id":"named","type":"line","source-layer":"transportation",
                          "filter":["has","name"],
                          "paint":{"line-color":"#111"}},
                         {"id":"bigroad","type":"line","source-layer":"transportation",
                          "filter":["in","class","motorway","trunk"],
                          "paint":{"line-color":"#222"}},
                         {"id":"lines","type":"line","source-layer":"transportation",
                          "filter":["==","$type","LineString"],
                          "paint":{"line-color":"#333"}})"))
                  .ok());

  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;

  // A road with NO brunnel tag: MapLibre's != is true for a missing tag, and
  // fvkit's kNotEqual on its own is not — the loader reconciles them, and
  // this is the assertion that says so.
  ASSERT_TRUE(e.Style(Road("motorway"), ctx, &out).ok());
  EXPECT_EQ(out.size(), 3u) << "notbridge + bigroad + lines";

  out.clear();
  fv::VectorFeature bridge = Road("motorway");
  bridge.attributes.push_back({"brunnel", "bridge"});
  ASSERT_TRUE(e.Style(bridge, ctx, &out).ok());
  EXPECT_EQ(out.size(), 2u) << "bigroad + lines";

  out.clear();
  fv::VectorFeature named = Road("minor");
  named.attributes.push_back({"name", "Peachtree St"});
  ASSERT_TRUE(e.Style(named, ctx, &out).ok());
  EXPECT_EQ(out.size(), 3u) << "notbridge + named + lines";

  // Same tags, point geometry: only the $type filter changes the answer.
  out.clear();
  fv::VectorFeature pt = Road("motorway");
  pt.type = fv::VectorGeometryType::kPoint;
  ASSERT_TRUE(e.Style(pt, ctx, &out).ok());
  EXPECT_EQ(out.size(), 2u) << "notbridge + bigroad";
}

TEST(OsmStyleFilter, NoneAndNestedAll) {
  fv::OsmStyleEngine e;
  ASSERT_TRUE(e
                  .LoadText(Wrap(
                      R"({"id":"x","type":"line","source-layer":"transportation",
                          "filter":["all",["none",["==","class","path"]],
                                          ["any",["==","class","motorway"],
                                                 ["==","class","trunk"]]],
                          "paint":{"line-color":"#000"}})"))
                  .ok());
  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e.Style(Road("trunk"), ctx, &out).ok());
  EXPECT_EQ(out.size(), 1u);
  out.clear();
  ASSERT_TRUE(e.Style(Road("path"), ctx, &out).ok());
  EXPECT_TRUE(out.empty());
  out.clear();
  ASSERT_TRUE(e.Style(Road("minor"), ctx, &out).ok());
  EXPECT_TRUE(out.empty());
}

// ---------------------------------------------------------------------------
// 3. Draw order is style-layer order
// ---------------------------------------------------------------------------

TEST(OsmStyleOrder, CasingSitsBelowItsOwnFill) {
  fv::OsmStyleEngine e;
  ASSERT_TRUE(e
                  .LoadText(Wrap(
                      R"({"id":"casing","type":"line","source-layer":"transportation",
                          "filter":["==","class","motorway"],
                          "paint":{"line-color":"#804020","line-width":10}},
                         {"id":"fill","type":"line","source-layer":"transportation",
                          "filter":["==","class","motorway"],
                          "paint":{"line-color":"#ffcc88","line-width":6}})"))
                  .ok());
  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e.Style(Road("motorway"), ctx, &out).ok());
  ASSERT_EQ(out.size(), 2u);
  // ONE feature, TWO passes, and the priorities are the style-layer indices —
  // which is what makes VectorScene's cross-feature stable sort draw every
  // casing in the viewport before any fill.
  EXPECT_EQ(out[0].priority, 0);
  EXPECT_EQ(out[1].priority, 1);
  EXPECT_EQ(out[0].stroke.pen.width, 10);
  EXPECT_EQ(out[1].stroke.pen.width, 6);
}

// ---------------------------------------------------------------------------
// Symbols, labels, circles
// ---------------------------------------------------------------------------

TEST(OsmStyleSymbol, LabelsAreOffByDefaultAndExpandTokens) {
  fv::OsmStyleEngine e;
  ASSERT_TRUE(e
                  .LoadText(Wrap(
                      R"({"id":"place","type":"symbol","source-layer":"place",
                          "layout":{"text-field":"{name:latin}","text-size":14},
                          "paint":{"text-color":"#333333"}})"))
                  .ok());
  fv::VectorFeature city;
  city.type = fv::VectorGeometryType::kPoint;
  city.layer = "place";
  city.attributes.push_back({"name:latin", "Atlanta"});
  city.parts.push_back({{33.75, -84.39}});

  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e.Style(city, ctx, &out).ok());
  EXPECT_TRUE(out.empty()) << "labels default off (host font, golden hashes)";

  e.SetDrawLabels(true);
  out.clear();
  ASSERT_TRUE(e.Style(city, ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  ASSERT_TRUE(out[0].label.valid);
  EXPECT_EQ(out[0].label.text, "Atlanta");
  EXPECT_DOUBLE_EQ(out[0].label.style.size, 14.0);

  // A feature with no such tag produces no blank label, and says so.
  fv::VectorFeature anon = city;
  anon.attributes.clear();
  out.clear();
  ASSERT_TRUE(e.Style(anon, ctx, &out).ok());
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(e.empty_labels(), 1u);
}

TEST(OsmStyleSymbol, LinePlacementCarriesTheRoadNameOntoItsRoad) {
  fv::OsmStyleEngine e;
  e.SetDrawLabels(true);
  ASSERT_TRUE(
      e.LoadText(Wrap(
           R"({"id":"road-name","type":"symbol",
               "source-layer":"transportation_name",
               "layout":{"text-field":"{name}","text-size":12,
                         "symbol-placement":"line","symbol-spacing":300,
                         "text-max-angle":30,"text-offset":[0,-0.5]},
               "paint":{"text-color":"#5a5a5a"}})"))
          .ok());
  fv::VectorFeature road;
  road.type = fv::VectorGeometryType::kLine;
  road.layer = "transportation_name";
  road.attributes.push_back({"name", "Meeting St"});
  road.parts.push_back({{32.78, -79.93}, {32.79, -79.93}});

  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e.Style(road, ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  const fv::LabelStyle& lb = out[0].label;
  ASSERT_TRUE(lb.valid);
  EXPECT_EQ(lb.placement, fv::LabelPlacement::kAlongPath);
  EXPECT_DOUBLE_EQ(lb.spacing_px, 300.0);
  EXPECT_DOUBLE_EQ(lb.max_angle_deg, 30.0);
  // GL's text-offset is in EMs with +y DOWN; the placer's offset is pixels,
  // positive to the LEFT of travel. A -0.5 em offset must therefore come out
  // POSITIVE, at half the text size.
  EXPECT_DOUBLE_EQ(lb.offset_px, 6.0);
}

TEST(OsmStyleSymbol, HaloWidthAndColourCrossTheSeamInDevicePixels) {
  fv::OsmStyleEngine e;
  e.SetDrawLabels(true);
  ASSERT_TRUE(
      e.LoadText(Wrap(
           R"J({"id":"place","type":"symbol","source-layer":"place",
               "layout":{"text-field":"{name}","text-size":14},
               "paint":{"text-color":"#333333","text-halo-blur":1,
                        "text-halo-color":"rgba(255,255,255,0.75)",
                        "text-halo-width":2}})J"))
          .ok());
  // Accepted, not rejected — and counted, so "why is my blur not soft?" has an
  // answer that is not a guess.
  EXPECT_EQ(e.ignored_halo_blur(), 1u);

  fv::VectorFeature city;
  city.type = fv::VectorGeometryType::kPoint;
  city.layer = "place";
  city.attributes.push_back({"name", "Atlanta"});
  city.parts.push_back({{33.75, -84.39}});

  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e.Style(city, ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  EXPECT_DOUBLE_EQ(out[0].label.halo_width, 2.0);
  EXPECT_EQ(out[0].label.halo_color.r, 255);
  EXPECT_EQ(out[0].label.halo_color.g, 255);
  EXPECT_EQ(out[0].label.halo_color.b, 255);
  EXPECT_NEAR(out[0].label.halo_color.a, 191, 1) << "0.75 alpha, straight";

  // CSS px at 96 dpi, like every other paint value: on a 192 dpi display the
  // halo doubles with the widths and sizes beside it.
  ctx.device_dpi = 192.0;
  out.clear();
  ASSERT_TRUE(e.Style(city, ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  EXPECT_DOUBLE_EQ(out[0].label.halo_width, 4.0);
}

// A colour with no width is not a request to draw an outline — GL's default
// text-halo-width is 0 and this loader must not invent one, or every style
// that sets a halo colour for one zoom band grows an outline in all of them.
TEST(OsmStyleSymbol, AHaloColourAloneDrawsNoHalo) {
  fv::OsmStyleEngine e;
  e.SetDrawLabels(true);
  ASSERT_TRUE(
      e.LoadText(Wrap(
           R"({"id":"place","type":"symbol","source-layer":"place",
               "layout":{"text-field":"{name}","text-size":14},
               "paint":{"text-color":"#333333","text-halo-color":"#ffffff"}})"))
          .ok());
  fv::VectorFeature city;
  city.type = fv::VectorGeometryType::kPoint;
  city.layer = "place";
  city.attributes.push_back({"name", "Atlanta"});
  city.parts.push_back({{33.75, -84.39}});

  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e.Style(city, ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  EXPECT_DOUBLE_EQ(out[0].label.halo_width, 0.0);
  EXPECT_EQ(e.ignored_halo_blur(), 0u);
}

// The halo is a zoom function like anything else in `paint`, and a stop table
// is the form OpenMapTiles styles write it in.
TEST(OsmStyleSymbol, HaloWidthTakesAZoomFunction) {
  fv::OsmStyleEngine e;
  e.SetDrawLabels(true);
  e.SetZoomOverride(6.0);
  ASSERT_TRUE(
      e.LoadText(Wrap(
           R"({"id":"place","type":"symbol","source-layer":"place",
               "layout":{"text-field":"{name}","text-size":14},
               "paint":{"text-color":"#333333",
                        "text-halo-width":{"base":1,
                                           "stops":[[4,1],[8,3]]}}})"))
          .ok());
  fv::VectorFeature city;
  city.type = fv::VectorGeometryType::kPoint;
  city.layer = "place";
  city.attributes.push_back({"name", "Atlanta"});
  city.parts.push_back({{33.75, -84.39}});

  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e.Style(city, ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  EXPECT_DOUBLE_EQ(out[0].label.halo_width, 2.0) << "halfway between the stops";
  // No colour given: white, GL's default, so the common case of a light halo
  // on dark text needs the width alone.
  EXPECT_EQ(out[0].label.halo_color.r, 255);
  EXPECT_EQ(out[0].label.halo_color.a, 255);
}

TEST(OsmStyleSymbol, PointPlacementIsStillTheDefaultAndUnknownOnesAreRejected) {
  fv::OsmStyleEngine e;
  e.SetDrawLabels(true);
  ASSERT_TRUE(e
                  .LoadText(Wrap(
                      R"({"id":"place","type":"symbol","source-layer":"place",
                          "layout":{"text-field":"{name}","text-size":12}})"))
                  .ok());
  fv::VectorFeature city;
  city.type = fv::VectorGeometryType::kPoint;
  city.layer = "place";
  city.attributes.push_back({"name", "Charleston"});
  city.parts.push_back({{32.78, -79.93}});
  std::vector<fv::StyleResult> out;
  fv::StyleContext ctx;
  ASSERT_TRUE(e.Style(city, ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].label.placement, fv::LabelPlacement::kPoint);

  // A placement this port does not model is a load-time rejection, not a
  // silent fallback to `point` — the declared-subset rule from O2.
  fv::OsmStyleEngine bad;
  EXPECT_FALSE(
      bad.LoadText(Wrap(
             R"({"id":"x","type":"symbol","source-layer":"place",
                 "layout":{"text-field":"{name}","symbol-placement":"radial"}})"))
          .ok());
}

TEST(OsmStyleSymbol, IconsAreRecordedNotDrawn) {
  fv::OsmStyleEngine e;
  e.SetDrawLabels(true);
  ASSERT_TRUE(e
                  .LoadText(Wrap(
                      R"({"id":"poi","type":"symbol","source-layer":"poi",
                          "layout":{"icon-image":"restaurant-11"}})"))
                  .ok());
  fv::VectorFeature poi;
  poi.type = fv::VectorGeometryType::kPoint;
  poi.layer = "poi";
  poi.parts.push_back({{33.75, -84.39}});
  std::vector<fv::StyleResult> out;
  fv::StyleContext ctx;
  ASSERT_TRUE(e.Style(poi, ctx, &out).ok());
  EXPECT_TRUE(out.empty());
  ASSERT_EQ(e.ignored_icons().size(), 1u);
  EXPECT_EQ(e.ignored_icons().begin()->first, "restaurant-11");
  EXPECT_EQ(e.ignored_icons().begin()->second, 1u);
}

TEST(OsmStyleCircle, BecomesAGeneratedEllipseDisplayList) {
  fv::OsmStyleEngine e;
  ASSERT_TRUE(e
                  .LoadText(Wrap(
                      R"({"id":"dot","type":"circle","source-layer":"poi",
                          "paint":{"circle-color":"#8b6f52","circle-radius":3}})"))
                  .ok());
  fv::VectorFeature poi;
  poi.type = fv::VectorGeometryType::kPoint;
  poi.layer = "poi";
  poi.parts.push_back({{33.75, -84.39}});
  std::vector<fv::StyleResult> out;
  fv::StyleContext ctx;
  ASSERT_TRUE(e.Style(poi, ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  ASSERT_TRUE(out[0].symbol.valid);

  const fv::VectorSymbol* sym = e.Symbol(out[0].symbol.symbol_id);
  ASSERT_NE(sym, nullptr);
  ASSERT_EQ(sym->primitives.size(), 1u);
  EXPECT_EQ(sym->primitives[0].type, fv::SymbolPrimitiveType::kEllipse);
  EXPECT_TRUE(sym->primitives[0].has_fill);
  EXPECT_EQ(int(sym->primitives[0].fill_color.r), 0x8B);
  // 3 CSS px at 96 dpi, in HIMETRIC (25.4 per pixel — renderer.h's fixed
  // px_per_himetric, inverted).
  EXPECT_NEAR(sym->primitives[0].radius1.x, 76.0, 0.5);
  EXPECT_DOUBLE_EQ(sym->primitives[0].radius2.x, 0.0);

  // Cached, and the same id resolves to the same object.
  EXPECT_EQ(e.Symbol(out[0].symbol.symbol_id), sym);
  EXPECT_EQ(e.symbols_cached(), 1u);

  // An id this engine cannot make is a counted miss, not a crash.
  EXPECT_EQ(e.Symbol("restaurant-11"), nullptr);
  EXPECT_EQ(e.unresolved_symbols().count("restaurant-11"), 1u);
}

TEST(OsmStyleEngineBasics, ClosedEngineAndStyleEpoch) {
  fv::OsmStyleEngine e;
  EXPECT_FALSE(e.IsOpen());
  std::vector<fv::StyleResult> out;
  fv::StyleContext ctx;
  EXPECT_FALSE(e.Style(Road("motorway"), ctx, &out).ok());

  ASSERT_TRUE(e.LoadText(Wrap("")).ok());
  EXPECT_TRUE(e.IsOpen());
  const uint64_t epoch = e.style_epoch();
  // Anything that changes what Style() returns must move the epoch, or the
  // retained VectorScene draws stale symbology.
  e.SetReferenceLatitude(33.75);
  EXPECT_NE(e.style_epoch(), epoch);
  const uint64_t e2 = e.style_epoch();
  e.SetReferenceLatitude(33.75);  // a no-op setter must stay a no-op
  EXPECT_EQ(e.style_epoch(), e2);
  e.SetDrawLabels(true);
  EXPECT_NE(e.style_epoch(), e2);
}

// ---------------------------------------------------------------------------
// The delivered reference style, and the real pyramid
// ---------------------------------------------------------------------------

TEST(OsmReferenceStyle, LoadsAndStaysInsideTheSubset) {
  const std::string path = StylePath();
  if (path.empty()) GTEST_SKIP() << "no FVW_OSM_STYLE_DIR";
  fv::OsmStyleEngine e;
  std::string err;
  ASSERT_TRUE(e.LoadFile(path, &err).ok()) << err;
  EXPECT_EQ(e.style_name(), "Peregrine OSM (OpenMapTiles)");
  EXPECT_GE(e.layers().size(), 25u);

  fv::FvColor bg;
  EXPECT_TRUE(e.background(0.0, &bg));
  EXPECT_EQ(int(bg.r), 0xF8);

  // The casing/fill construction the schema's road rendering depends on:
  // every casing layer must come before every road fill layer.
  const fv::OsmStyleLayerInfo* casing = Info(e, "road-motorway-casing");
  const fv::OsmStyleLayerInfo* fill = Info(e, "road-minor");
  ASSERT_NE(casing, nullptr);
  ASSERT_NE(fill, nullptr);
  EXPECT_LT(casing->priority, fill->priority);
}

// A casing and the line it cases must resolve to the SAME dash pixels, or the
// two passes lay their dashes on different stretches of road and the casing
// reads as a second dashed line rather than as a halo. A GL dasharray is in
// LINE-WIDTH units, so the casing's array has to be its parent's divided by
// the width ratio — and the ratio has to hold at every zoom, which means the
// two width curves must be proportional. This is a property of the delivered
// style files, not of the renderer, so it is asserted on the files.
//
// THE CASING IS THE FIRST DASHED PASS, not a colour. Layer order is what makes
// a casing a casing, and a sheet is free to draw one in any colour it likes —
// style.json's cycleway casing is a translucent dark brown, not the white a
// halo over pale ground would be.
TEST(OsmReferenceStyle, ACasingDashesInStepWithTheLineItCases) {
  const char* dir = getenv("FVW_OSM_STYLE_DIR");
  if (dir == nullptr) GTEST_SKIP() << "no FVW_OSM_STYLE_DIR";

  // class=path + subclass=cycleway is what both cycleway layers filter on.
  fv::VectorFeature way;
  way.type = fv::VectorGeometryType::kLine;
  way.layer = "transportation";
  way.style_key = "cycleway";
  way.attributes.push_back({"class", "path"});
  way.attributes.push_back({"subclass", "cycleway"});
  way.parts.push_back({{33.75, -84.39}, {33.76, -84.38}});

  for (const char* name : {"style.json", "OldEurope.json"}) {
    const std::string path = std::string(dir) + "/" + name;
    if (!fs::is_regular_file(path)) continue;
    fv::OsmStyleEngine e;
    std::string err;
    ASSERT_TRUE(e.LoadFile(path, &err).ok()) << name << ": " << err;
    e.SetReferenceLatitude(33.75);

    for (double z : {8.0, 12.0, 16.0, 18.0, 20.0}) {
      fv::StyleContext ctx;
      ctx.scale_denominator = fv::webmerc::ScaleForZoomExact(z, 33.75, 0.25);
      std::vector<fv::StyleResult> out;
      ASSERT_TRUE(e.Style(way, ctx, &out).ok());

      // The first two dashed passes, in the order the layers declared them:
      // the casing, then the line it cases.
      const std::vector<double>* casing = nullptr;
      const std::vector<double>* line = nullptr;
      for (const fv::StyleResult& r : out) {
        if (!r.stroke.valid || r.stroke.pen.dash.empty()) continue;
        if (casing == nullptr) {
          casing = &r.stroke.pen.dash;
          continue;
        }
        line = &r.stroke.pen.dash;
        break;
      }
      // A sheet with no dashed cycleway PAIR at this zoom has nothing to say
      // about the invariant — one dashed pass is a plain dashed line, and no
      // dashed pass is a cycleway this sheet draws solid or not at all.
      if (casing == nullptr || line == nullptr) continue;
      ASSERT_EQ(casing->size(), line->size()) << name << " z" << z;
      for (size_t i = 0; i < casing->size(); ++i)
        EXPECT_NEAR((*casing)[i], (*line)[i], 1e-9)
            << name << " z" << z << " run " << i;
    }
  }
}

TEST(OsmReferenceStyle, StylesRealAtlantaFeatures) {
  const std::string style = StylePath();
  const std::string mb = MbtilesPath();
  if (style.empty() || mb.empty()) GTEST_SKIP() << "no style or no mbtiles";

  fv::OsmVectorSource src;
  ASSERT_TRUE(src.Open(mb).ok());
  fv::OsmStyleEngine eng;
  ASSERT_TRUE(eng.LoadFile(style).ok());

  // The two halves must agree about what a scale means, which is the whole
  // point of the shared relation: same pitch, and the engine's reference
  // latitude is the viewport's.
  const fv::GeoRect area{{33.7440, -84.3960}, {33.7600, -84.3800}};
  const double lat = 0.5 * (area.ll.lat + area.ur.lat);
  eng.SetReferenceLatitude(lat);
  eng.SetDisplayMmPerPixel(src.display_mm_per_pixel());

  fv::VectorQuery q;
  q.area = area;
  q.scale_denominator = fv::webmerc::ScaleForZoomExact(
      14, lat, src.display_mm_per_pixel());
  std::vector<fv::VectorFeature> features;
  ASSERT_TRUE(src.Query(q, &features).ok());
  ASSERT_FALSE(features.empty());
  EXPECT_EQ(src.last_query_zoom(), 14);
  EXPECT_NEAR(eng.ZoomForScale(q.scale_denominator), 14.0, 1e-9);

  fv::StyleContext ctx;
  ctx.scale_denominator = q.scale_denominator;
  size_t styled = 0, passes = 0;
  int prev_priority = -1;
  bool nondecreasing_per_feature = true;
  for (const fv::VectorFeature& f : features) {
    std::vector<fv::StyleResult> out;
    ASSERT_TRUE(eng.Style(f, ctx, &out).ok());
    if (out.empty()) continue;
    ++styled;
    passes += out.size();
    prev_priority = -1;
    for (const fv::StyleResult& r : out) {
      if (r.priority < prev_priority) nondecreasing_per_feature = false;
      prev_priority = r.priority;
    }
  }
  // A structural assertion plus a lower bound, never a total over a whole
  // directory (the ledger's golden rule).
  EXPECT_GT(styled, 100u);
  EXPECT_GE(passes, styled);
  EXPECT_TRUE(nondecreasing_per_feature)
      << "Style() must append in style-layer order";
  EXPECT_GT(eng.layers_that_drew(), 4u);
  EXPECT_TRUE(eng.unresolved_symbols().empty());
}

// ---------------------------------------------------------------------------
// 9. Sprites: icons and pattern fills (O6)
// ---------------------------------------------------------------------------
//
// Everything here is SYNTHETIC — a two-sprite sheet written to a scratch dir —
// because no sprite set ships with the port and the assertions are about the
// wiring, not about anybody's artwork.

namespace {

class SpriteDir {
 public:
  explicit SpriteDir(const std::string& name)
      : path_(fs::temp_directory_path() / ("fv_osmsprite_" + name)) {
    std::error_code ec;
    fs::remove_all(path_, ec);
    fs::create_directories(path_, ec);
  }
  ~SpriteDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  std::string file(const std::string& n) const { return (path_ / n).string(); }

  // `hatch` is 8x4 and `dot` is 8x8, so a test can tell the two apart by the
  // spacing a pattern fill derives from the tile.
  void WriteSheet(const std::string& stem = "sprite") const {
    fv::PixelBuffer sheet(16, 8);
    for (int y = 0; y < 8; ++y) {
      unsigned char* row = sheet.Row(y);
      for (int x = 0; x < 16; ++x) {
        const bool left = x < 8;
        row[x * 4 + 0] = left ? 255 : 0;
        row[x * 4 + 1] = left ? 0 : 255;
        row[x * 4 + 2] = 0;
        row[x * 4 + 3] = 255;
      }
    }
    EXPECT_TRUE(fv::WritePng(sheet, file(stem + ".png")).ok());
    std::ofstream(file(stem + ".json"))
        << R"({"dot":   {"x": 0, "y": 0, "width": 8, "height": 8},)"
        << R"( "hatch": {"x": 8, "y": 0, "width": 8, "height": 4}})";
  }

 private:
  fs::path path_;
};

fv::VectorFeature Poi(const char* klass) {
  fv::VectorFeature f;
  f.type = fv::VectorGeometryType::kPoint;
  f.layer = "poi";
  f.style_key = klass;
  f.attributes.push_back({"class", klass});
  f.parts.push_back({{33.75, -84.39}});
  return f;
}

fv::VectorFeature Landuse(const char* klass) {
  fv::VectorFeature f;
  f.type = fv::VectorGeometryType::kArea;
  f.layer = "landuse";
  f.style_key = klass;
  f.attributes.push_back({"class", klass});
  f.parts.push_back({{33.75, -84.39}, {33.76, -84.39}, {33.76, -84.38},
                     {33.75, -84.38}, {33.75, -84.39}});
  return f;
}

}  // namespace

TEST(OsmSprite, AnIconLayerDrawsItsSpriteInsteadOfBeingCounted) {
  SpriteDir dir("icon");
  dir.WriteSheet();
  fv::OsmStyleEngine e;
  e.SetSpriteBase(dir.file("sprite"));
  ASSERT_TRUE(e.LoadText(Wrap(
      R"({"id":"poi","type":"symbol","source-layer":"poi",
          "layout":{"icon-image":"dot"}})")).ok());
  EXPECT_EQ(e.sprite_ids().size(), 2u);

  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e.Style(Poi("cafe"), ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  EXPECT_TRUE(out[0].symbol.valid) << "the icon half of a symbol layer";
  EXPECT_EQ(out[0].symbol.symbol_id, "sprite:dot");
  // The whole point of O6: this used to be the only outcome.
  EXPECT_TRUE(e.ignored_icons().empty());

  // And it RESOLVES — through Pixmap(), not Symbol(), because a sprite has no
  // display list. Both halves matter: a symbol_id the renderer cannot resolve
  // draws nothing at all.
  EXPECT_EQ(e.Symbol("sprite:dot"), nullptr);
  const fv::SymbolPixmap* tile = e.Pixmap("sprite:dot");
  ASSERT_NE(tile, nullptr);
  EXPECT_EQ(tile->tile.Width(), 8);
  EXPECT_EQ(tile->tile.Height(), 8);

  // AND asking for the display list must not have counted the sprite as
  // unresolved symbology. The base class counts a failed LoadSymbol, so
  // without the short-circuit in Symbol() every icon that drew perfectly
  // would be listed in the one diagnostic that means "this did not draw".
  EXPECT_TRUE(e.unresolved_symbols().empty())
      << "a sprite resolves through Pixmap(); that is not a miss";
}

TEST(OsmSprite, AnIconNameIsATokenTemplateLikeATextField) {
  // `"icon-image": "{class}"` is how OpenMapTiles styles key a POI icon off
  // the feature, and it is far more common than a literal name.
  SpriteDir dir("token");
  dir.WriteSheet();
  fv::OsmStyleEngine e;
  e.SetSpriteBase(dir.file("sprite"));
  ASSERT_TRUE(e.LoadText(Wrap(
      R"({"id":"poi","type":"symbol","source-layer":"poi",
          "layout":{"icon-image":"{class}"}})")).ok());

  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e.Style(Poi("dot"), ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].symbol.symbol_id, "sprite:dot");

  // A class the sheet has no sprite for is counted and drawn as nothing —
  // which is what ignored_icons() means now.
  out.clear();
  ASSERT_TRUE(e.Style(Poi("helipad"), ctx, &out).ok());
  EXPECT_TRUE(out.empty());
  ASSERT_EQ(e.ignored_icons().count("helipad"), 1u);
}

TEST(OsmSprite, IconSizeAndRotateReachTheSymbol) {
  SpriteDir dir("sizerot");
  dir.WriteSheet();
  fv::OsmStyleEngine e;
  e.SetSpriteBase(dir.file("sprite"));
  ASSERT_TRUE(e.LoadText(Wrap(
      R"({"id":"poi","type":"symbol","source-layer":"poi",
          "layout":{"icon-image":"dot","icon-size":1.5,"icon-rotate":30}})"))
                  .ok());

  fv::StyleContext ctx;
  ctx.device_dpi = 96.0;  // dpi_scale == 1, so the multiplier stands alone
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e.Style(Poi("cafe"), ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  EXPECT_DOUBLE_EQ(out[0].symbol.scale, 1.5);
  EXPECT_DOUBLE_EQ(out[0].symbol.rotation_deg, 30.0);
}

TEST(OsmSprite, AnIconAndAName_AreTwoHalvesOfOneLayer) {
  // A GL symbol layer can carry both, and before O6 the icon half was dropped
  // — so a layer with an icon and NO text contributed nothing at all.
  SpriteDir dir("both");
  dir.WriteSheet();
  fv::OsmStyleEngine e;
  e.SetSpriteBase(dir.file("sprite"));
  ASSERT_TRUE(e.LoadText(Wrap(
      R"({"id":"poi","type":"symbol","source-layer":"poi",
          "layout":{"icon-image":"dot","text-field":"{class}"},
          "paint":{"text-color":"#000"}})")).ok());

  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;
  e.SetDrawLabels(true);
  ASSERT_TRUE(e.Style(Poi("cafe"), ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u) << "one layer is still one pass";
  EXPECT_TRUE(out[0].symbol.valid);
  EXPECT_TRUE(out[0].label.valid);
  EXPECT_EQ(out[0].label.text, "cafe");

  // The halves are independent BOTH ways: the label switch must not take the
  // icon down with it. (It is off by default, which is why the icon has to be
  // emitted before the `draw_labels` early-out and not after.)
  e.SetDrawLabels(false);
  out.clear();
  ASSERT_TRUE(e.Style(Poi("cafe"), ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  EXPECT_TRUE(out[0].symbol.valid) << "labels off must not hide the icon";
  EXPECT_FALSE(out[0].label.valid);
}

TEST(OsmSprite, AFillPatternSpacesItselfByItsOwnTile) {
  // GL tiles a fill-pattern seamlessly, so the stamp lattice's spacing has to
  // be the tile's own size — anything else is a field of stamps with gaps.
  SpriteDir dir("pattern");
  dir.WriteSheet();
  fv::OsmStyleEngine e;
  e.SetSpriteBase(dir.file("sprite"));
  ASSERT_TRUE(e.LoadText(Wrap(
      R"({"id":"lu","type":"fill","source-layer":"landuse",
          "paint":{"fill-pattern":"hatch"}})")).ok());

  fv::StyleContext ctx;
  ctx.device_dpi = 96.0;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e.Style(Landuse("wood"), ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  ASSERT_TRUE(out[0].area_pattern.valid);
  EXPECT_EQ(out[0].area_pattern.symbol_id, "sprite:hatch");
  // `hatch` is 8 wide and 4 tall: the spacing is the tile, not a square.
  EXPECT_DOUBLE_EQ(out[0].area_pattern.spacing_x, 8.0);
  EXPECT_DOUBLE_EQ(out[0].area_pattern.spacing_y, 4.0);
  EXPECT_FALSE(out[0].area_pattern.staggered) << "GL tiles on a plain grid";
  EXPECT_DOUBLE_EQ(out[0].area_pattern.symbol_scale, 1.0);
}

TEST(OsmSprite, APatternsSpacingAndItsStampScaleTogether) {
  // THE SEAMLESSNESS INVARIANT: the lattice step must equal the tile's DRAWN
  // size, so the spacing and the scale have to carry the same factors. Scaling
  // only the spacing (which is what this did first) opens a gutter of exactly
  // the missing factor at every device DPI but 96, turning a texture fill into
  // a grid of stamps.
  SpriteDir dir("patdpi");
  dir.WriteSheet();
  fv::OsmStyleEngine e;
  e.SetSpriteBase(dir.file("sprite"));
  ASSERT_TRUE(e.LoadText(Wrap(
      R"({"id":"lu","type":"fill","source-layer":"landuse",
          "paint":{"fill-pattern":"hatch"}})")).ok());

  for (double dpi : {96.0, 192.0, 144.0}) {
    fv::StyleContext ctx;
    ctx.device_dpi = dpi;
    std::vector<fv::StyleResult> out;
    ASSERT_TRUE(e.Style(Landuse("wood"), ctx, &out).ok());
    ASSERT_EQ(out.size(), 1u) << dpi;
    const fv::AreaPatternStyle& ap = out[0].area_pattern;
    ASSERT_TRUE(ap.valid) << dpi;
    // The tile is 8x4 at pixelRatio 1, so the drawn size is 8*scale by
    // 4*scale and the step must be exactly that in both axes.
    EXPECT_DOUBLE_EQ(ap.spacing_x, 8.0 * ap.symbol_scale) << "dpi " << dpi;
    EXPECT_DOUBLE_EQ(ap.spacing_y, 4.0 * ap.symbol_scale) << "dpi " << dpi;
    // And the DPI really is reaching it, or the assertion above is vacuous.
    EXPECT_DOUBLE_EQ(ap.symbol_scale, dpi / 96.0) << "dpi " << dpi;
  }
}

TEST(OsmSprite, APatternAndAColourCanBothBeAsked) {
  // fill-color under fill-pattern is legal and common: the colour shows
  // wherever the artwork is transparent, so both slots are filled.
  SpriteDir dir("patcol");
  dir.WriteSheet();
  fv::OsmStyleEngine e;
  e.SetSpriteBase(dir.file("sprite"));
  ASSERT_TRUE(e.LoadText(Wrap(
      R"({"id":"lu","type":"fill","source-layer":"landuse",
          "paint":{"fill-color":"#abcdef","fill-pattern":"dot"}})")).ok());

  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e.Style(Landuse("wood"), ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  EXPECT_TRUE(out[0].fill.valid);
  EXPECT_EQ(out[0].fill.brush.color.r, 0xAB);
  EXPECT_TRUE(out[0].area_pattern.valid);
}

TEST(OsmSprite, AStyleWithNoSheetStillLoadsAndCountsItsIcons) {
  // The web case: `sprite` is an https URL, nothing here fetches, and failing
  // the style over it would reject every style published on the internet.
  fv::OsmStyleEngine e;
  ASSERT_TRUE(e.LoadText(
                   R"({"version":8,"name":"t",
                       "sprite":"https://example.invalid/sprite",
                       "layers":[{"id":"poi","type":"symbol",
                                  "source-layer":"poi",
                                  "layout":{"icon-image":"dot"}}]})")
                  .ok());
  EXPECT_EQ(e.sprite_url(), "https://example.invalid/sprite");
  EXPECT_TRUE(e.sprite_base().empty()) << "nothing here fetches";
  EXPECT_TRUE(e.sprite_ids().empty());

  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e.Style(Poi("cafe"), ctx, &out).ok());
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(e.ignored_icons().count("dot"), 1u);
}

TEST(OsmSprite, ASpriteBaseTheCallerNamedMustActuallyLoad) {
  // The asymmetry is deliberate: a base the CALLER asserted is a caller error
  // when it does not open, while a style's own missing sheet is not.
  SpriteDir dir("explicit");
  fv::OsmStyleEngine e;
  e.SetSpriteBase(dir.file("nothing-here"));
  std::string err;
  EXPECT_FALSE(e.LoadText(Wrap(
      R"({"id":"poi","type":"symbol","source-layer":"poi",
          "layout":{"icon-image":"dot"}})"), &err).ok());
  EXPECT_NE(err.find("SetSpriteBase"), std::string::npos) << err;
}

TEST(OsmSprite, ARelativeSpriteResolvesBesideTheStyleFile) {
  SpriteDir dir("relative");
  dir.WriteSheet("icons");
  std::ofstream(dir.file("style.json"))
      << R"({"version":8,"name":"t","sprite":"icons","layers":[)"
      << R"({"id":"poi","type":"symbol","source-layer":"poi",)"
      << R"("layout":{"icon-image":"dot"}}]})";

  fv::OsmStyleEngine e;
  ASSERT_TRUE(e.LoadFile(dir.file("style.json")).ok());
  EXPECT_EQ(e.sprite_ids().size(), 2u) << "resolved next to its style";

  fv::StyleContext ctx;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e.Style(Poi("cafe"), ctx, &out).ok());
  ASSERT_EQ(out.size(), 1u);
  EXPECT_TRUE(out[0].symbol.valid);
}

TEST(OsmSprite, LinePatternAndBackgroundPatternAreStillRejectedAndSayWhy) {
  // The declared-subset rule: the sheet loading now, these fail for their OWN
  // reasons, and the message has to carry the new reason rather than the old
  // "no sprite sheet" one.
  fv::OsmStyleEngine e;
  std::string err;
  EXPECT_FALSE(e.LoadText(Wrap(
      R"({"id":"r","type":"line","source-layer":"transportation",
          "paint":{"line-pattern":"dot"}})"), &err).ok());
  EXPECT_EQ(err.find("does not load"), std::string::npos) << err;
  EXPECT_NE(err.find("stretches"), std::string::npos) << err;

  EXPECT_FALSE(e.LoadText(Wrap(
      R"({"id":"bg","type":"background",
          "paint":{"background-pattern":"dot"}})"), &err).ok());
  EXPECT_NE(err.find("canvas clear"), std::string::npos) << err;
}

// The end-to-end half: a sprite named by a style has to reach the CANVAS.
// Everything above stops at the StyleResult, and the seam between the two is
// exactly where this module's one real bug lived — ResolveSymbol asks
// Symbol() before Pixmap(), and answering the first one wrongly makes an icon
// that styles perfectly draw nothing at all.
namespace {

class OneFeatureSource : public fv::IVectorSource {
 public:
  std::vector<fv::VectorFeature> features;
  fv::Status Open(const std::string&) override { return fv::Status::Ok(); }
  bool IsOpen() const override { return true; }
  fv::GeoRect Bounds() const override { return fv::GeoRect::World(); }
  std::vector<std::string> Layers() const override { return {"poi"}; }
  fv::Status Query(const fv::VectorQuery&,
                   std::vector<fv::VectorFeature>* out) override {
    for (const auto& f : features) out->push_back(f);
    return fv::Status::Ok();
  }
};

}  // namespace

TEST(OsmSprite, ASpriteReachesTheCanvasThroughTheRenderer) {
  SpriteDir dir("render");
  dir.WriteSheet();
  auto style = std::make_shared<fv::OsmStyleEngine>();
  style->SetSpriteBase(dir.file("sprite"));
  ASSERT_TRUE(style->LoadText(Wrap(
      R"({"id":"poi","type":"symbol","source-layer":"poi",
          "layout":{"icon-image":"dot"}})")).ok());

  auto src = std::make_shared<OneFeatureSource>();
  src->features.push_back(Poi("cafe"));

  fv::MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(64, 64).ok());
  ASSERT_TRUE(proj.SetCenter(fv::GeoPoint{33.75, -84.39}).ok());
  ASSERT_TRUE(proj.SetResolution(0.001, 0.001).ok());

  fv::CpuCanvas canvas(64, 64);
  canvas.Clear(fv::FvColor{255, 255, 255, 255});
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(proj, &canvas).ok());

  // `dot` is the solid RED half of the sheet, so its ink is unmistakable.
  int red = 0;
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) {
      const unsigned char* px = canvas.Buffer().Row(y) + 4 * x;
      if (px[0] > 200 && px[1] < 60 && px[2] < 60) ++red;
    }
  EXPECT_EQ(red, 64) << "the whole 8x8 sprite, blitted once";
  EXPECT_EQ(r.draws_emitted(), 1u);
  EXPECT_TRUE(style->unresolved_symbols().empty());
  EXPECT_TRUE(style->ignored_icons().empty());
}

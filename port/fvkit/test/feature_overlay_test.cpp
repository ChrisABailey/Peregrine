// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/overlay/feature_overlay.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

#include "fvkit/app/type_registry.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/feature/vector_source.h"

namespace {

using fv::FeatureOverlay;
using fv::Geometry;
using fv::GeoPoint;

namespace fs = std::filesystem;

const std::string kRivers = FV_FEATURE_RIVERS;

fv::MapProjection Harbour(double scale = 250000.0) {
  fv::MapProjection p;
  p.SetSurfaceSize(400, 400);
  p.SetCenter({32.86, -79.95});
  p.SetScale(scale);
  return p;
}

/// Pixels that are no longer white.
int Inked(const fv::CpuCanvas& c) {
  int n = 0;
  const fv::PixelBuffer& b = c.Buffer();
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int x = 0; x < b.Width(); ++x)
      if (row[4 * x] != 255 || row[4 * x + 1] != 255 || row[4 * x + 2] != 255) ++n;
  }
  return n;
}

const unsigned char* Px(const fv::CpuCanvas& c, int x, int y) { return c.Buffer().Row(y) + 4 * x; }

std::string Temp(const std::string& name) {
  const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
  return (fs::temp_directory_path() / (std::string("fv_fo_") + info->name() + "_" + name)).string();
}

TEST(FeatureOverlay, RegisteredForGeoPackageAndGeoJson) {
  fv::app::OverlayTypeRegistry registry;
  ASSERT_TRUE(fv::app::RegisterBuiltinOverlayTypes(registry).ok());
  for (const char* ext : {"gpkg", ".GPKG", "geojson", ".GeoJSON"}) {
    const auto* d = registry.FindByExtension(ext);
    ASSERT_NE(d, nullptr) << ext;
    EXPECT_EQ(d->id, FeatureOverlay::kTypeId);
  }
  const auto* d = registry.Find(FeatureOverlay::kTypeId);
  ASSERT_NE(d, nullptr);
  ASSERT_EQ(d->file->save_filters.size(), 2u);
  EXPECT_EQ(d->file->save_filters[FeatureOverlay::kFormatGeoJson].second, "*.geojson");
}

TEST(FeatureOverlay, DrawsTheRiversSample) {
  FeatureOverlay overlay;
  const fv::Status s = overlay.FileOpen(kRivers);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(overlay.Name(), "Charleston rivers");
  ASSERT_EQ(overlay.layer()->Count(), 3u);

  fv::CpuCanvas canvas(400, 400);
  canvas.Clear({255, 255, 255, 255});
  const fv::MapProjection proj = Harbour();
  ASSERT_TRUE(overlay.OnDraw(proj, canvas).ok());
  EXPECT_EQ(overlay.features_drawn(), 3u);
  const int inked = Inked(canvas);
  EXPECT_GT(inked, 300);
  // The Cooper's mouth, 32.80 N 79.92 W, is drawn in the line colour.
  double x = 0, y = 0;
  ASSERT_TRUE(proj.GeoToSurface({32.80, -79.92}, &x, &y).ok());
  const unsigned char* p = Px(canvas, static_cast<int>(x + 0.5), static_cast<int>(y + 0.5));
  EXPECT_LT(p[0], 100);
  EXPECT_GT(p[2], 150);
}

TEST(FeatureOverlay, OnlyFeaturesInViewAreDrawn) {
  FeatureOverlay overlay;
  ASSERT_TRUE(overlay.FileOpen(kRivers).ok());
  fv::CpuCanvas canvas(400, 400);
  canvas.Clear({255, 255, 255, 255});
  // Zoomed onto the upper Ashley, far from the other two.
  fv::MapProjection proj;
  proj.SetSurfaceSize(400, 400);
  proj.SetCenter({32.89, -80.08});
  proj.SetScale(20000.0);
  ASSERT_TRUE(overlay.OnDraw(proj, canvas).ok());
  EXPECT_EQ(overlay.features_drawn(), 1u);
}

TEST(FeatureOverlay, OpacityFadesAndZeroHides) {
  FeatureOverlay overlay;
  ASSERT_TRUE(overlay.FileOpen(kRivers).ok());
  const fv::MapProjection proj = Harbour();
  double x = 0, y = 0;
  ASSERT_TRUE(proj.GeoToSurface({32.80, -79.92}, &x, &y).ok());
  const int px = static_cast<int>(x + 0.5), py = static_cast<int>(y + 0.5);

  fv::CpuCanvas full(400, 400), half(400, 400), none(400, 400);
  for (fv::CpuCanvas* c : {&full, &half, &none}) c->Clear({255, 255, 255, 255});
  ASSERT_TRUE(overlay.OnDraw(proj, full).ok());
  EXPECT_TRUE(overlay.SupportsOpacity());
  overlay.SetOpacity(50);
  ASSERT_TRUE(overlay.OnDraw(proj, half).ok());
  overlay.SetOpacity(0);
  ASSERT_TRUE(overlay.OnDraw(proj, none).ok());
  EXPECT_EQ(Inked(none), 0);
  EXPECT_EQ(overlay.features_drawn(), 0u);
  // Red over white: 30 at full strength, about halfway to 255 at 50%.
  EXPECT_LT(Px(full, px, py)[0], 60);
  EXPECT_GT(Px(half, px, py)[0], 110);
  EXPECT_LT(Px(half, px, py)[0], 180);
  overlay.SetOpacity(140);
  EXPECT_EQ(overlay.opacity(), 100);
}

TEST(FeatureOverlay, AreaHolesStayUnfilled) {
  fv::FeatureSchema schema;
  schema.geometry_type = fv::GeometryType::kPolygon;
  auto layer = std::make_shared<fv::MemoryFeatureLayer>("marsh", schema);
  fv::Feature f;
  f.geometry = Geometry::Polygon({{{32.80, -80.00}, {32.80, -79.90}, {32.92, -79.90}, {32.92, -80.00}},
                                  {{32.84, -79.97}, {32.84, -79.93}, {32.88, -79.93}, {32.88, -79.97}}});
  ASSERT_TRUE(layer->Insert(&f).ok());
  FeatureOverlay overlay;
  overlay.SetLayer(layer);
  EXPECT_EQ(overlay.Name(), "marsh");
  fv::FeatureStyle style;
  style.symbol.area_fill = {0, 160, 0, 255};
  overlay.SetStyle(style);

  fv::CpuCanvas canvas(400, 400);
  canvas.Clear({255, 255, 255, 255});
  const fv::MapProjection proj = Harbour();
  ASSERT_TRUE(overlay.OnDraw(proj, canvas).ok());
  double x = 0, y = 0;
  ASSERT_TRUE(proj.GeoToSurface({32.86, -79.95}, &x, &y).ok());  // inside the hole
  EXPECT_EQ(Px(canvas, static_cast<int>(x), static_cast<int>(y))[1], 255);
  EXPECT_EQ(Px(canvas, static_cast<int>(x), static_cast<int>(y))[0], 255);
  ASSERT_TRUE(proj.GeoToSurface({32.82, -79.95}, &x, &y).ok());  // in the ring
  EXPECT_EQ(Px(canvas, static_cast<int>(x), static_cast<int>(y))[1], 160);
  EXPECT_EQ(Px(canvas, static_cast<int>(x), static_cast<int>(y))[0], 0);
}

TEST(FeatureOverlay, PointsDrawAsMarkers) {
  fv::FeatureSchema schema;
  auto layer = std::make_shared<fv::MemoryFeatureLayer>("pins", schema);
  fv::Feature f;
  f.geometry = Geometry::Point({32.86, -79.95});
  ASSERT_TRUE(layer->Insert(&f).ok());
  FeatureOverlay overlay;
  overlay.SetLayer(layer);
  fv::CpuCanvas canvas(400, 400);
  canvas.Clear({255, 255, 255, 255});
  ASSERT_TRUE(overlay.OnDraw(Harbour(), canvas).ok());
  EXPECT_EQ(Px(canvas, 200, 200)[0], 220);  // the default marker fill, at the centre
  EXPECT_GT(Inked(canvas), 40);
  EXPECT_LT(Inked(canvas), 200);
}

TEST(FeatureOverlay, SavesAsGeoPackageAndGeoJsonAndReopens) {
  FeatureOverlay overlay;
  ASSERT_TRUE(overlay.FileOpen(kRivers).ok());
  const std::string gpkg = Temp("rivers.gpkg");
  const std::string json = Temp("rivers.geojson");
  fs::remove(gpkg);
  fs::remove(json);
  fv::Status s = overlay.FileSaveAs(gpkg, FeatureOverlay::kFormatGeoPackage);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(overlay.layer()->Count(), 3u);
  EXPECT_EQ(overlay.Name(), "Charleston rivers");
  EXPECT_FALSE(fs::exists(gpkg + ".saving"));

  // An edit to the GeoPackage is held until Save; Revert drops it.
  fv::Feature cooper;
  ASSERT_TRUE(overlay.layer()->Get(1, &cooper).ok());
  ASSERT_TRUE(overlay.layer()->Delete(1).ok());
  EXPECT_EQ(overlay.layer()->Count(), 2u);
  ASSERT_TRUE(overlay.Revert(gpkg).ok());
  EXPECT_EQ(overlay.layer()->Count(), 3u);
  ASSERT_TRUE(overlay.layer()->Delete(1).ok());
  ASSERT_TRUE(overlay.FileSaveAs(gpkg, FeatureOverlay::kFormatGeoPackage).ok());  // plain Save

  FeatureOverlay reopened;
  ASSERT_TRUE(reopened.FileOpen(gpkg).ok());
  EXPECT_EQ(reopened.layer()->Count(), 2u);
  EXPECT_EQ(reopened.layer()->Schema(), overlay.layer()->Schema());

  ASSERT_TRUE(reopened.FileSaveAs(json, FeatureOverlay::kFormatGeoJson).ok());
  FeatureOverlay from_json;
  ASSERT_TRUE(from_json.FileOpen(json).ok());
  EXPECT_EQ(from_json.layer()->Count(), 2u);
  EXPECT_EQ(from_json.layer()->Schema(), overlay.layer()->Schema());
  EXPECT_EQ(reopened.FileSaveAs(json, 7).code, fv::kUnsupported);
  fs::remove(gpkg);
  fs::remove(json);
}

TEST(FeatureOverlay, OpenFailuresAreReported) {
  FeatureOverlay overlay;
  EXPECT_FALSE(overlay.FileOpen("/no/such/file.gpkg").ok());
  EXPECT_FALSE(overlay.FileOpen("/no/such/file.geojson").ok());
  const std::string empty = Temp("empty.gpkg");
  fs::remove(empty);
  {
    std::shared_ptr<fv::GeoPackage> gp;
    ASSERT_TRUE(fv::GeoPackage::Create(empty, &gp).ok());
  }
  EXPECT_EQ(overlay.FileOpen(empty).code, fv::kNotFound);
  EXPECT_EQ(overlay.layer(), nullptr);
  fs::remove(empty);
  ASSERT_TRUE(overlay.FileNew().ok());
  EXPECT_EQ(overlay.layer()->Count(), 0u);
}

}  // namespace

namespace {

TEST(FeatureOverlay, DisplayScaleWidensTheStyle) {
  FeatureOverlay overlay;
  ASSERT_TRUE(overlay.FileOpen(kRivers).ok());
  fv::CpuCanvas one(400, 400), two(400, 400);
  one.Clear({255, 255, 255, 255});
  two.Clear({255, 255, 255, 255});
  ASSERT_TRUE(overlay.OnDraw(Harbour(), one).ok());
  overlay.SetDisplayScale(2.0);
  ASSERT_TRUE(overlay.OnDraw(Harbour(), two).ok());
  EXPECT_GT(Inked(two), Inked(one) * 3 / 2);
}

}  // namespace

// --- rendering through MapLibreStyleEngine ---------------------------------

TEST(FeatureVectorSource, SplitsMultiPartsAndCarriesIdAndFields) {
  fv::FeatureSchema schema;
  schema.fields.push_back({});
  schema.fields[0].name = "kind";
  schema.fields.push_back({});
  schema.fields[1].name = "flow";
  schema.fields[1].type = fv::FieldType::kReal;
  fv::Feature f;
  f.id = 7;
  f.values = {std::string("marsh"), fv::FieldValue{}};
  f.geometry = Geometry::Collection(
      fv::GeometryType::kMultiPolygon,
      {Geometry::Polygon({{{32.0, -80.0}, {32.0, -79.9}, {32.1, -79.9}},
                          {{32.02, -79.98}, {32.02, -79.95}, {32.05, -79.95}}}),
       Geometry::Polygon({{{33.0, -80.0}, {33.0, -79.9}, {33.1, -79.9}}})});
  std::vector<fv::VectorFeature> out;
  fv::FeatureToVector(f, schema, &out);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].type, fv::VectorGeometryType::kArea);
  EXPECT_EQ(out[0].parts.size(), 2u);  // exterior and hole
  EXPECT_EQ(out[1].parts.size(), 1u);
  for (const fv::VectorFeature& v : out) {
    EXPECT_TRUE(v.has_id);
    EXPECT_EQ(v.id, 7);
    EXPECT_EQ(v.layer, fv::kFeatureSourceLayer);
    ASSERT_NE(v.Attribute("kind"), nullptr);
    EXPECT_EQ(*v.Attribute("kind"), "marsh");
    EXPECT_EQ(v.Attribute("flow"), nullptr);  // null is absent, so ["get"] is null
  }
}

/// One point at the view centre, drawn with `sym`.
std::unique_ptr<fv::CpuCanvas> DrawPoint(const fv::FeatureSymbol& sym, int64_t select = 0) {
  auto layer = std::make_shared<fv::MemoryFeatureLayer>("pins", fv::FeatureSchema{});
  fv::Feature f;
  f.geometry = Geometry::Point({32.86, -79.95});
  EXPECT_TRUE(layer->Insert(&f).ok());
  FeatureOverlay overlay;
  overlay.SetLayer(layer);
  fv::FeatureStyle style;
  style.symbol = sym;
  overlay.SetStyle(style);
  overlay.SetSelected(select == 0 ? 0 : f.id);
  auto canvas = std::make_unique<fv::CpuCanvas>(400, 400);
  canvas->Clear({255, 255, 255, 255});
  EXPECT_TRUE(overlay.OnDraw(Harbour(), *canvas).ok());
  return canvas;
}

TEST(FeatureOverlay, SquareMarkersFillTheirCorners) {
  fv::FeatureSymbol sym;
  sym.marker_size_px = 20;
  sym.marker_outline_px = 0;
  EXPECT_EQ(Px(*DrawPoint(sym), 192, 192)[0], 255);  // outside the disc
  sym.marker = fv::FeatureMarker::kSquare;
  EXPECT_EQ(Px(*DrawPoint(sym), 192, 192)[0], 220);
}

TEST(FeatureOverlay, TheSelectedFeatureGetsAYellowHalo) {
  fv::FeatureSymbol sym;
  sym.marker_outline_px = 0;
  auto yellow = [](const fv::CpuCanvas& c) {
    int n = 0;
    for (int y = 180; y < 220; ++y)
      for (int x = 180; x < 220; ++x)
        if (Px(c, x, y)[0] == 255 && Px(c, x, y)[1] == 200 && Px(c, x, y)[2] == 0) ++n;
    return n;
  };
  EXPECT_EQ(yellow(*DrawPoint(sym)), 0);
  const auto selected = DrawPoint(sym, 1);
  EXPECT_GT(yellow(*selected), 20);
  EXPECT_EQ(Px(*selected, 200, 200)[0], 220);  // the marker draws over its halo
}

TEST(FeatureOverlay, GraduatedWidthsAndDashPresetsGoThroughTheEngine) {
  fv::FeatureSchema schema;
  schema.fields.push_back({});
  schema.fields[0].name = "flow";
  schema.fields[0].type = fv::FieldType::kReal;
  auto layer = std::make_shared<fv::MemoryFeatureLayer>("rivers", schema);
  fv::Feature low, high;
  low.values = {1.0};
  low.geometry = Geometry::LineString({{32.84, -80.0}, {32.84, -79.9}});
  high.values = {50.0};
  high.geometry = Geometry::LineString({{32.88, -80.0}, {32.88, -79.9}});
  ASSERT_TRUE(layer->Insert(&low).ok());
  ASSERT_TRUE(layer->Insert(&high).ok());

  fv::FeatureStyle style;
  style.renderer = fv::FeatureRenderer::kGraduated;
  style.field = "flow";
  style.classes.resize(2);
  style.classes[0].lower = 0;
  style.classes[0].symbol.line_width_px = 1;
  style.classes[1].lower = 10;
  style.classes[1].symbol.line_width_px = 7;
  FeatureOverlay overlay;
  overlay.SetLayer(layer);
  overlay.SetStyle(style);
  const fv::MapProjection proj = Harbour();
  double x = 0, y_low = 0, y_high = 0;
  ASSERT_TRUE(proj.GeoToSurface({32.84, -79.95}, &x, &y_low).ok());
  ASSERT_TRUE(proj.GeoToSurface({32.88, -79.95}, &x, &y_high).ok());
  auto thickness = [&](const fv::CpuCanvas& c, double y) {
    int n = 0;
    for (int dy = -8; dy <= 8; ++dy)
      if (Px(c, static_cast<int>(x), static_cast<int>(std::lround(y)) + dy)[0] != 255) ++n;
    return n;
  };
  fv::CpuCanvas canvas(400, 400);
  canvas.Clear({255, 255, 255, 255});
  ASSERT_TRUE(overlay.OnDraw(proj, canvas).ok());
  EXPECT_EQ(thickness(canvas, y_low), 1);
  EXPECT_EQ(thickness(canvas, y_high), 7);
  EXPECT_EQ(overlay.features_drawn(), 2u);

  // A railroad is a dashed line with crossties stamped across it.
  for (fv::FeatureClass& c : style.classes) c.symbol.dash = "railroad";
  overlay.SetStyle(style);
  fv::CpuCanvas rail(400, 400);
  rail.Clear({255, 255, 255, 255});
  ASSERT_TRUE(overlay.OnDraw(proj, rail).ok());
  int across = 0;
  for (int px = 0; px < 400; ++px)
    if (Px(rail, px, static_cast<int>(std::lround(y_low)) - 3)[0] != 255) ++across;
  EXPECT_GT(across, 5) << "crossties stand off a 1 px line";
}

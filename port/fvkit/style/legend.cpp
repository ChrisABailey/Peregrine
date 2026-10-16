// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/style/legend.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "fvkit/geo/web_mercator.h"
#include "fvkit/proj.h"
#include "fvkit/vector/renderer.h"
#include "fvkit/vector/style.h"

namespace fv {

namespace {

/// MapLibre's CSS pixel: the device DPI at which one authored pixel is one pixel.
constexpr double kStyleNominalDpi = 96.0;

/// Serves a fixed set of features to every query.
class SpecimenSource : public IVectorSource {
 public:
  explicit SpecimenSource(std::vector<VectorFeature> features)
      : features_(std::move(features)) {}

  Status Open(const std::string&) override { return Status::Ok(); }
  bool IsOpen() const override { return true; }
  GeoRect Bounds() const override { return GeoRect::World(); }
  std::vector<std::string> Layers() const override {
    std::vector<std::string> out;
    for (const VectorFeature& f : features_)
      if (std::find(out.begin(), out.end(), f.layer) == out.end())
        out.push_back(f.layer);
    return out;
  }
  Status Query(const VectorQuery&, std::vector<VectorFeature>* out) override {
    *out = features_;
    return Status::Ok();
  }

 private:
  std::vector<VectorFeature> features_;
};

/// A specimen as a feature with no geometry, for styling alone.
VectorFeature SpecimenFeature(const MapLibreLegendSpecimen& sp) {
  VectorFeature f;
  f.type = sp.type;
  f.layer = sp.layer;
  f.attributes = sp.tags;
  return f;
}

/// Places `f` on the swatch: a line across the full width (overshooting so
/// its caps are clipped off), a point at the centre, an area inset from the
/// edges. Coordinates come from `proj`, so the geometry is exactly as wide
/// on the ground as the swatch is at this scale.
void PlaceSpecimen(const MapProjection& proj, int width_px, int height_px,
                   VectorFeature* f) {
  auto geo = [&proj](double x, double y) {
    GeoPoint p{};
    proj.SurfaceToGeo(x, y, &p);
    return p;
  };
  const double mid = height_px / 2.0;
  std::vector<GeoPoint> part;
  switch (f->type) {
    case VectorGeometryType::kLine:
      part = {geo(-0.25 * width_px, mid), geo(1.25 * width_px, mid)};
      break;
    case VectorGeometryType::kPoint:
      part = {geo(width_px / 2.0, mid)};
      break;
    case VectorGeometryType::kArea: {
      const double ix = 0.15 * width_px, iy = 0.15 * height_px;
      part = {geo(ix, iy), geo(width_px - ix, iy), geo(width_px - ix, height_px - iy),
              geo(ix, height_px - iy), geo(ix, iy)};
      break;
    }
  }
  GeoRect b{part.front(), part.front()};
  for (const GeoPoint& p : part) {
    b.ll.lat = std::min(b.ll.lat, p.lat);
    b.ur.lat = std::max(b.ur.lat, p.lat);
    b.ll.lon = std::min(b.ll.lon, p.lon);
    b.ur.lon = std::max(b.ur.lon, p.lon);
  }
  f->parts = {std::move(part)};
  f->bounds = b;
}

}  // namespace

std::vector<std::string> LegendEntryLayers(MapLibreStyleEngine& engine,
                                           const MapLibreLegendEntry& entry,
                                           double scale_denominator) {
  StyleContext ctx;
  ctx.scale_denominator = scale_denominator;
  std::vector<int> priorities;
  for (const MapLibreLegendSpecimen& sp : entry.specimen) {
    std::vector<StyleResult> out;
    if (!engine.Style(SpecimenFeature(sp), ctx, &out).ok()) continue;
    for (const StyleResult& r : out) priorities.push_back(r.priority);
  }
  std::vector<std::string> ids;
  for (const MapLibreLayerInfo& info : engine.layers())
    if (std::find(priorities.begin(), priorities.end(), info.priority) != priorities.end())
      ids.push_back(info.id);
  return ids;
}

bool LegendEntryOnMap(const std::vector<std::string>& entry_layers,
                      const std::vector<std::string>& map_layers) {
  if (entry_layers.empty()) return false;
  for (const std::string& id : entry_layers)
    if (std::find(map_layers.begin(), map_layers.end(), id) == map_layers.end()) return false;
  return true;
}

double LegendEntryMinZoom(MapLibreStyleEngine& engine, const MapLibreLegendEntry& entry) {
  const double z22 = webmerc::ScaleForZoomExact(22.0, engine.reference_latitude(),
                                                engine.display_mm_per_pixel());
  double min_zoom = -1.0;
  for (const std::string& id : LegendEntryLayers(engine, entry, z22)) {
    for (const MapLibreLayerInfo& info : engine.layers()) {
      if (info.id != id) continue;
      if (min_zoom < 0.0 || info.minzoom < min_zoom) min_zoom = info.minzoom;
    }
  }
  return min_zoom;
}

std::vector<std::string> RenderLegendSwatch(MapLibreStyleEngine& engine,
                                            const MapLibreLegendEntry& entry,
                                            double scale_denominator,
                                            double pixels_per_point, int width_pt,
                                            int height_pt, ICanvas* canvas) {
  if (canvas == nullptr || !(pixels_per_point > 0.0) || width_pt <= 0 || height_pt <= 0)
    return {};
  const int width_px = static_cast<int>(std::lround(width_pt * pixels_per_point));
  const int height_px = static_cast<int>(std::lround(height_pt * pixels_per_point));

  FvColor bg{255, 255, 255, 255};
  engine.background(scale_denominator, &bg);
  canvas->Clear(bg);

  // The map's arrangement: geometry placed on surface pixels, the style's
  // zoom read per point, and an authored pixel drawn as one point.
  MapProjection proj;
  if (!proj.SetSurfaceSize(width_px, height_px).ok()) return {};
  if (!proj.SetCenter(GeoPoint{engine.reference_latitude(), 0.0}).ok()) return {};
  if (!proj.SetPhysicalScale(scale_denominator,
                             engine.display_mm_per_pixel() / pixels_per_point)
           .ok())
    return {};

  std::vector<VectorFeature> features;
  for (const MapLibreLegendSpecimen& sp : entry.specimen) {
    VectorFeature f = SpecimenFeature(sp);
    PlaceSpecimen(proj, width_px, height_px, &f);
    features.push_back(std::move(f));
  }

  // The engine is borrowed for the duration of the call.
  StyleEnginePtr style(&engine, [](IStyleEngine*) {});
  VectorRenderer renderer(std::make_shared<SpecimenSource>(std::move(features)), style);
  renderer.SetDeviceDpi(kStyleNominalDpi * pixels_per_point);
  if (!renderer.Render(proj, canvas).ok()) return {};
  return LegendEntryLayers(engine, entry, scale_denominator);
}

}  // namespace fv

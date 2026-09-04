// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The Terrain Avoidance Mask overlay — see fvkit/overlay/ta_mask_overlay.h.
// Plan: port/tamask-plan.md.

#include "fvkit/overlay/ta_mask_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "fvkit/canvas/geo_draw.h"
#include "fvkit/scale_table.h"
#include "fvkit/symbol/builtin.h"

namespace fv {
namespace {

using app::PropertySpec;
using app::PropertyType;
using app::PropertyValue;

enum Prop {
  kDisplayThreshold = 0,
  kLabelThreshold,
  kAltitude,
  kUnit,
  kWarnClearance,
  kCautionClearance,
  kOkClearance,
  kShowWarnLevel,
  kShowCautionLevel,
  kShowOkLevel,
  kWarnColor,
  kCautionColor,
  kOkColor,
  kShowNoDataMask,
  kNoDataColor,
  kShading,
  kDrawMaskProp,
  kDrawContoursProp,
  kContourWidthPx,
  kShowPeak,
  kPeakColor,
  kShowLabels,
  kLabelSizePx,
  kLabelColor,
  kLabelHaloColor,
  kLabelFont,
  kSensitivity,
  kMaxSamplesPerDraw,
  kPropCount
};

enum Unit { kFeet = 0, kMeters = 1 };
constexpr double kMetersPerFoot = 0.3048;

PropertySpec Spec(const char* key, const char* label, const char* group,
                  PropertyValue def, double lo = 0.0, double hi = 0.0,
                  const char* help = "") {
  PropertySpec s;
  s.key = key;
  s.label = label;
  s.group = group;
  s.type = def.type;
  s.default_value = def;
  s.min = lo;
  s.max = hi;
  s.help = help;
  return s;
}

const std::vector<PropertySpec>& Specs() {
  static const std::vector<PropertySpec> specs = [] {
    std::vector<PropertySpec> v;
    v.reserve(kPropCount);
    v.push_back(Spec("display_threshold", "Show at 1:N or larger", "Data",
                     PropertyValue::Double(2000000.0), 1000.0, 50000000.0,
                     "FalconView's default is 1:2 M -- COARSER than the "
                     "contour overlay's 1:250 K, because a coloured mask "
                     "still reads at a scale where contour lines are a "
                     "smear."));
    v.push_back(Spec("label_threshold", "Label at 1:N or larger", "Data",
                     PropertyValue::Double(500000.0), 1000.0, 50000000.0));
    v.push_back(Spec("altitude", "Altitude MSL", "Aircraft",
                     PropertyValue::Double(2500.0), -1500.0, 120000.0,
                     "In whichever unit `unit` names. FalconView's TestAlt: "
                     "the altitude to use when no overlay is feeding one."));
    PropertySpec unit = Spec("unit", "Unit", "Aircraft",
                             PropertyValue::Choice(kFeet));
    unit.type = PropertyType::kChoice;
    unit.choices = {"feet", "meters"};
    unit.help =
        "What the altitude, the clearances and the labels are written in. "
        "The terrain is metres either way.";
    v.push_back(unit);
    v.push_back(Spec("warn_clearance", "Warning clearance", "Bands",
                     PropertyValue::Double(100.0), 0.0, 50000.0,
                     "Ground within this of the aircraft is a warning."));
    v.push_back(Spec("caution_clearance", "Caution clearance", "Bands",
                     PropertyValue::Double(300.0), 0.0, 50000.0));
    v.push_back(Spec("ok_clearance", "OK clearance", "Bands",
                     PropertyValue::Double(500.0), 0.0, 50000.0));
    v.push_back(Spec("show_warn_level", "Show warning band", "Bands",
                     PropertyValue::Bool(true), 0, 0,
                     "A band that is switched off gives its ground to the "
                     "band BELOW it, which is what FalconView's cascading "
                     "colour indices do -- so turning off 'caution' makes "
                     "that ground green, not blank."));
    v.push_back(Spec("show_caution_level", "Show caution band", "Bands",
                     PropertyValue::Bool(true)));
    v.push_back(Spec("show_ok_level", "Show OK band", "Bands",
                     PropertyValue::Bool(true)));
    v.push_back(Spec("warn_color", "Warning colour", "Bands",
                     PropertyValue::Color(FvColor{255, 0, 0, 255})));
    v.push_back(Spec("caution_color", "Caution colour", "Bands",
                     PropertyValue::Color(FvColor{255, 255, 0, 255})));
    v.push_back(Spec("ok_color", "OK colour", "Bands",
                     PropertyValue::Color(FvColor{0, 255, 0, 255})));
    v.push_back(Spec("show_no_data_mask", "Show no-data mask", "Bands",
                     PropertyValue::Bool(true), 0, 0,
                     "Voids INSIDE the coverage, drawn whether or not the "
                     "fill is: a hole in the terrain under an aircraft is the "
                     "most important thing on the screen. Ground the source "
                     "does not cover at all is left alone."));
    v.push_back(Spec("no_data_color", "No-data colour", "Bands",
                     PropertyValue::Color(FvColor{255, 0, 128, 255}),
                     0, 0, "FalconView's RGB(255, 0, 128)."));
    v.push_back(Spec("shading", "Mask shading", "Mask",
                     PropertyValue::Int(50), 0, 100,
                     "Percent opacity of the fill over the base map. "
                     "FalconView's 50 is the 128 it hands its alpha "
                     "blitter."));
    v.push_back(Spec("draw_mask", "Draw the mask", "Mask",
                     PropertyValue::Bool(true)));
    v.push_back(Spec("draw_contours", "Draw the band outlines", "Mask",
                     PropertyValue::Bool(false), 0, 0,
                     "Contour lines at the same three levels as the fill. "
                     "Off by default, as in FalconView."));
    v.push_back(Spec("contour_width_px", "Outline width", "Mask",
                     PropertyValue::Int(2), 1, 8, "pixels"));
    v.push_back(Spec("show_peak", "Mark the highest point", "Peak",
                     PropertyValue::Bool(true)));
    v.push_back(Spec("peak_color", "Peak marker colour", "Peak",
                     PropertyValue::Color(FvColor{0, 0, 0, 255})));
    v.push_back(Spec("show_labels", "Show labels", "Peak",
                     PropertyValue::Bool(true)));
    v.push_back(Spec("label_size_px", "Label size", "Peak",
                     PropertyValue::Double(12.0), 6.0, 48.0, "pixels"));
    v.push_back(Spec("label_color", "Label colour", "Peak",
                     PropertyValue::Color(FvColor{0, 0, 0, 255})));
    v.push_back(Spec("label_halo_color", "Label outline", "Peak",
                     PropertyValue::Color(FvColor{255, 255, 255, 220})));
    v.push_back(Spec("label_font", "Label font", "Peak",
                     PropertyValue::String(std::string()),
                     0, 0, "Path to a TTF/TTC file; empty = the canvas's own."));
    v.push_back(Spec("sensitivity", "Altitude dead band", "Aircraft",
                     PropertyValue::Double(25.0), 0.0, 5000.0,
                     "A live altitude that jitters must not repaint the map "
                     "continuously, so SetAltitude ignores a move smaller "
                     "than this. Over in FalconView it also guarded a cache "
                     "rebuild; here the cache holds ELEVATION and the "
                     "colouring is per frame, so this damps a repaint and "
                     "nothing else."));
    v.push_back(Spec("max_samples_per_draw", "Sample budget", "Data",
                     PropertyValue::Int(1000000), 10000, 40000000,
                     "A safety valve, not a tuning knob: elevation posts read "
                     "in one draw. Tiles beyond it are left for the next "
                     "draw."));
    return v;
  }();
  return specs;
}

int IndexOf(const std::string& key) {
  const std::vector<PropertySpec>& specs = Specs();
  for (size_t i = 0; i < specs.size(); ++i)
    if (specs[i].key == key) return static_cast<int>(i);
  return -1;
}

// Longitude folded back into (-180, 180]. Called once per mask pixel, so it is
// two comparisons rather than a fmod: a scanline can only walk off the end of
// the world once.
inline double WrapLon(double lon) {
  while (lon > 180.0) lon -= 360.0;
  while (lon <= -180.0) lon += 360.0;
  return lon;
}

}  // namespace

// ---------------------------------------------------------------------------
// The classifier — the whole of the overlay's meaning, in eight lines
// ---------------------------------------------------------------------------
//
// FalconView spells this three times (ConvertAltitudeToColor, the mask loop in
// draw_to_base_map, and the level arguments to TraceClearanceContours) and the
// three spellings agree by luck rather than by construction. Here the fill,
// the outlines and any future readout all call this.
ClearanceBand ClassifyClearance(float elevation_m, const ClearanceLevels& l) {
  if (std::isnan(elevation_m)) return ClearanceBand::kNoData;
  const double e = elevation_m;
  // Top down, and a band that is switched off falls through to the one below
  // rather than punching a hole: FalconView's WarnColorIdx defaults to
  // CautionColorIdx defaults to OKColorIdx for exactly this.
  if (l.show_warn && e >= l.warn_m) return ClearanceBand::kWarn;
  if (l.show_caution && e >= l.caution_m) return ClearanceBand::kCaution;
  if (l.show_ok && e >= l.ok_m) return ClearanceBand::kOk;
  return ClearanceBand::kNone;
}

const char TAMaskOverlay::kTypeId[] = "fv.tamask";

TAMaskOverlay::TAMaskOverlay() : Overlay("tamask") {
  values_.reserve(Specs().size());
  for (const PropertySpec& s : Specs()) values_.push_back(s.default_value);
}

void TAMaskOverlay::SetElevationSource(std::shared_ptr<IElevationSource> src) {
  source_ = std::move(src);
  ClearCache();
}

void TAMaskOverlay::ClearCache() {
  tiles_.clear();
  tiling_.Reset();
}

const std::vector<app::PropertySpec>& TAMaskOverlay::Describe() const {
  return Specs();
}

Status TAMaskOverlay::GetProperty(const std::string& key,
                                  app::PropertyValue* out) const {
  const int i = IndexOf(key);
  if (i < 0) return Status::Error(kNotFound, "no tamask property '" + key + "'");
  if (out != nullptr) *out = values_[static_cast<size_t>(i)];
  return Status::Ok();
}

Status TAMaskOverlay::SetProperty(const std::string& key,
                                  const app::PropertyValue& value) {
  const int i = IndexOf(key);
  if (i < 0) return Status::Error(kNotFound, "no tamask property '" + key + "'");
  const PropertySpec& spec = Specs()[static_cast<size_t>(i)];
  if (value.type != spec.type)
    return Status::Error(kInvalidArg,
                         "wrong type for tamask property '" + key + "'");
  if (spec.min != spec.max) {
    const double v = spec.type == PropertyType::kDouble
                         ? value.d
                         : static_cast<double>(value.i);
    if (v < spec.min || v > spec.max)
      return Status::Error(kInvalidArg,
                           "tamask property '" + key + "' out of range");
  }
  if (spec.type == PropertyType::kChoice &&
      (value.i < 0 || value.i >= static_cast<long long>(spec.choices.size())))
    return Status::Error(kInvalidArg,
                         "tamask property '" + key + "' has no such choice");
  values_[static_cast<size_t>(i)] = value;
  // NOTHING here invalidates the cache, and that is the point of caching
  // elevation rather than colour: an altitude, a clearance, a colour or a
  // band being switched off all change the picture and none of them changes
  // what was read off the disc.
  return Status::Ok();
}

double TAMaskOverlay::Altitude() const { return GetDouble("altitude", 2500.0); }

double TAMaskOverlay::AltitudeMeters() const {
  const double a = Altitude();
  return GetInt("unit", kFeet) == kFeet ? a * kMetersPerFoot : a;
}

bool TAMaskOverlay::SetAltitude(double altitude) {
  const double dead_band = GetDouble("sensitivity", 25.0);
  if (std::fabs(altitude - Altitude()) < dead_band) return false;
  return SetProperty("altitude", PropertyValue::Double(altitude)).ok();
}

ClearanceLevels TAMaskOverlay::Levels() const {
  const double to_m = GetInt("unit", kFeet) == kFeet ? kMetersPerFoot : 1.0;
  const double alt_m = AltitudeMeters();
  ClearanceLevels l;
  l.warn_m = alt_m - GetDouble("warn_clearance", 100.0) * to_m;
  l.caution_m = alt_m - GetDouble("caution_clearance", 300.0) * to_m;
  l.ok_m = alt_m - GetDouble("ok_clearance", 500.0) * to_m;
  l.show_warn = GetBool("show_warn_level", true);
  l.show_caution = GetBool("show_caution_level", true);
  l.show_ok = GetBool("show_ok_level", true);
  return l;
}

// ---------------------------------------------------------------------------
// Tiles
// ---------------------------------------------------------------------------

TAMaskOverlay::Tile* TAMaskOverlay::TileAt(TileIndex index,
                                           long long* budget) {
  const int64_t key = ElevationTiling::Key(index);
  auto it = tiles_.find(key);
  if (it != tiles_.end()) return &it->second;
  if (!tiling_.Valid(index)) return nullptr;

  const int nx = tiling_.PostsX();
  const int ny = tiling_.PostsY();
  const long long cost = static_cast<long long>(nx) * ny;
  if (cost > *budget) {
    ++stats_.tiles_over_budget;
    return nullptr;
  }

  Tile tile;
  tile.bounds = tiling_.BoundsOf(index);
  if (!SampleElevationGrid(*source_, tile.bounds, nx, ny, &tile.grid,
                           &tile.covered)
           .ok())
    return nullptr;
  *budget -= cost;
  stats_.samples += cost;
  ++stats_.tiles_sampled;

  // The tile's own peak, computed once here rather than per frame — this is
  // the one piece of FalconView's per-tile bookkeeping that pays for itself,
  // because it is what lets the on-screen peak search stop early (TA5).
  //
  // `has_data` is a cheap early-out for a tile the source answered nowhere;
  // the per-post `covered` mask above is what actually decides the no-data
  // colour.
  for (int r = 0; r < tile.grid.height; ++r) {
    for (int c = 0; c < tile.grid.width; ++c) {
      const float e = tile.grid.At(r, c);
      if (std::isnan(e)) continue;
      tile.has_data = true;
      if (!tile.has_max || e > tile.max_m) {
        tile.has_max = true;
        tile.max_m = e;
        tile.max_at = GeoPoint{tile.bounds.ll.lat + r * tile.grid.LatStep(),
                               tile.bounds.ll.lon + c * tile.grid.LonStep()};
      }
    }
  }

  auto ins = tiles_.emplace(key, std::move(tile));
  return &ins.first->second;
}

void TAMaskOverlay::EvictOutside(const GeoRect& keep) {
  for (auto it = tiles_.begin(); it != tiles_.end();) {
    if (it->second.bounds.Intersects(keep))
      ++it;
    else
      it = tiles_.erase(it);
  }
}

// ---------------------------------------------------------------------------
// The raster (plan TA4)
// ---------------------------------------------------------------------------
//
// One screen-sized RGBA buffer, one DrawPixmap, and no alignment arithmetic
// at all — where FalconView spends ~200 lines assembling per-tile byte masks
// into a 0.2-degree-aligned pixmap, sliding the on-screen sub-rectangle to
// the buffer's upper left and stretching the result.
//
// It can be this short because THE PROJECTION IS AFFINE (fvkit/proj.h:
// equal-arc, plus a rotation about the surface centre, which is linear). So
// the geographic step per pixel across a row and down a column are two
// constants, taken from three SurfaceToGeo calls rather than from a formula —
// whatever proj.h does about rotation, resolution mode or physical scale, the
// walk below stays exact because it asked.
Status TAMaskOverlay::DrawMask(const MapProjection& proj, ICanvas& canvas,
                               const std::vector<Tile*>& visible,
                               const ClearanceLevels& levels) {
  const PixelSize surf = proj.SurfaceSize();
  if (surf.width <= 0 || surf.height <= 0 || visible.empty())
    return Status::Ok();

  GeoPoint o, along_x, along_y;
  if (!proj.SurfaceToGeo(0.5, 0.5, &o).ok() ||
      !proj.SurfaceToGeo(1.5, 0.5, &along_x).ok() ||
      !proj.SurfaceToGeo(0.5, 1.5, &along_y).ok())
    return Status::Ok();
  const double dlat_dx = along_x.lat - o.lat;
  const double dlat_dy = along_y.lat - o.lat;
  // Unwrapped near the origin, or a step taken across the antimeridian comes
  // out as 360 degrees per pixel.
  const double dlon_dx = UnwrapLonNear(along_x.lon, o.lon) - o.lon;
  const double dlon_dy = UnwrapLonNear(along_y.lon, o.lon) - o.lon;

  const unsigned char alpha = static_cast<unsigned char>(
      std::lround(std::max<long long>(0, std::min<long long>(
                      100, GetInt("shading", 50))) * 255.0 / 100.0));
  const bool fill = GetBool("draw_mask", true);
  const bool no_data = GetBool("show_no_data_mask", true);
  FvColor band_color[5];
  band_color[static_cast<int>(ClearanceBand::kNone)] = FvColor{0, 0, 0, 0};
  band_color[static_cast<int>(ClearanceBand::kOk)] =
      GetColor("ok_color", FvColor{0, 255, 0, 255});
  band_color[static_cast<int>(ClearanceBand::kCaution)] =
      GetColor("caution_color", FvColor{255, 255, 0, 255});
  band_color[static_cast<int>(ClearanceBand::kWarn)] =
      GetColor("warn_color", FvColor{255, 0, 0, 255});
  band_color[static_cast<int>(ClearanceBand::kNoData)] =
      GetColor("no_data_color", FvColor{255, 0, 128, 255});

  PixelBuffer buf(surf.width, surf.height);
  // Every pixel is written, transparent included, so the buffer needs no
  // clearing pass of its own.
  const Tile* last = nullptr;  // the tile the previous pixel landed in

  for (int y = 0; y < surf.height; ++y) {
    double lat = o.lat + dlat_dx * 0.0 + dlat_dy * y;
    double lon = o.lon + dlon_dx * 0.0 + dlon_dy * y;
    unsigned char* row = buf.Row(y);
    for (int x = 0; x < surf.width; ++x, lat += dlat_dx, lon += dlon_dx) {
      unsigned char* px = row + x * 4;
      px[0] = px[1] = px[2] = px[3] = 0;

      const double wlon = WrapLon(lon);
      if (lat < -90.0 || lat > 90.0) continue;

      if (last == nullptr || lat < last->bounds.ll.lat ||
          lat > last->bounds.ur.lat || wlon < last->bounds.ll.lon ||
          wlon > last->bounds.ur.lon) {
        last = nullptr;
        for (const Tile* t : visible) {
          if (lat >= t->bounds.ll.lat && lat <= t->bounds.ur.lat &&
              wlon >= t->bounds.ll.lon && wlon <= t->bounds.ur.lon) {
            last = t;
            break;
          }
        }
        if (last == nullptr) continue;
      }
      if (!last->has_data) continue;

      // NEAREST POST, not interpolated. The mask is blocky on purpose and
      // FalconView aligns its blocks to the posts by hand so a mask square
      // agrees with the cursor elevation readout; rounding lands on the same
      // squares without the arithmetic.
      const ElevationGrid& g = last->grid;
      int c = static_cast<int>(
          std::lround((wlon - g.bounds.ll.lon) / g.LonStep()));
      int r = static_cast<int>(
          std::lround((lat - g.bounds.ll.lat) / g.LatStep()));
      c = std::max(0, std::min(g.width - 1, c));
      r = std::max(0, std::min(g.height - 1, r));

      const size_t i = static_cast<size_t>(r) * g.width + c;
      ClearanceBand band = ClassifyClearance(g.At(r, c), levels);
      // A NaN the source never claimed is not a hole in the terrain, it is
      // the edge of the data. Nothing is drawn there.
      if (band == ClearanceBand::kNoData && i < last->covered.size() &&
          last->covered[i] == 0)
        continue;
      ++stats_.band_pixels[static_cast<int>(band)];
      if (band == ClearanceBand::kNone) continue;
      if (band == ClearanceBand::kNoData ? !no_data : !fill) continue;

      const FvColor& col = band_color[static_cast<int>(band)];
      px[0] = col.r;
      px[1] = col.g;
      px[2] = col.b;
      // The band colours are opaque swatches and `shading` is what makes the
      // fill see-through, so the two multiply — a user who wants a translucent
      // warning colour and a solid no-data one can still say so.
      px[3] = static_cast<unsigned char>(col.a * alpha / 255);
      ++stats_.mask_pixels;
    }
  }
  return canvas.DrawPixmap(buf, 0, 0);
}

// ---------------------------------------------------------------------------
// The outlines
// ---------------------------------------------------------------------------

Status TAMaskOverlay::DrawContours(const MapProjection& proj, GeoDraw& gd,
                                   const std::vector<Tile*>& visible,
                                   const ClearanceLevels& levels) {
  (void)proj;
  // The band a traced line belongs to is its index into this list, which is
  // why TraceElevationContoursAtLevels hands the caller's own index back.
  std::vector<double> want;
  std::vector<FvColor> colors;
  if (levels.show_warn) {
    want.push_back(levels.warn_m);
    colors.push_back(GetColor("warn_color", FvColor{255, 0, 0, 255}));
  }
  if (levels.show_caution) {
    want.push_back(levels.caution_m);
    colors.push_back(GetColor("caution_color", FvColor{255, 255, 0, 255}));
  }
  if (levels.show_ok) {
    want.push_back(levels.ok_m);
    colors.push_back(GetColor("ok_color", FvColor{0, 255, 0, 255}));
  }
  if (want.empty()) return Status::Ok();

  const int width = static_cast<int>(GetInt("contour_width_px", 2));
  std::vector<bool> level_drawn(colors.size(), false);
  Status first = Status::Ok();
  for (Tile* t : visible) {
    if (!t->has_data) continue;
    if (t->traced_levels != want) {
      t->lines = TraceElevationContoursAtLevels(t->grid, want);
      t->traced_levels = want;
    }
    for (const ContourLine& line : t->lines) {
      if (line.level_index < 0 ||
          line.level_index >= static_cast<int>(colors.size()))
        continue;
      ++stats_.contour_lines;
      level_drawn[static_cast<size_t>(line.level_index)] = true;
      stats_.contour_vertices += static_cast<int>(line.points.size());
      const Status s = gd.DrawGeoPolyline(
          line.points, LineKind::kSimple,
          SolidGeoLine(colors[static_cast<size_t>(line.level_index)], width));
      if (!s.ok() && first.ok()) first = s;
    }
  }
  for (bool drawn : level_drawn)
    if (drawn) ++stats_.contour_levels;
  return first;
}

// ---------------------------------------------------------------------------
// The peak (plan TA5)
// ---------------------------------------------------------------------------
//
// FalconView's algorithm, kept, because it is the one piece of its
// bookkeeping that pays for itself: visit the tiles highest-own-maximum
// first, stop at the first tile whose maximum is actually on screen, and only
// scan a tile's posts when its own maximum is off screen. Its early exit is
// the same one: once the best found beats the next tile's own maximum,
// nothing further down the list can win.
Status TAMaskOverlay::DrawPeak(const MapProjection& proj, GeoDraw& gd,
                               const std::vector<Tile*>& visible,
                               bool with_label) {
  const PixelSize surf = proj.SurfaceSize();
  auto on_screen = [&](const GeoPoint& p) {
    double x = 0, y = 0;
    if (!proj.GeoToSurface(p, &x, &y).ok()) return false;
    return x >= 0 && y >= 0 && x <= surf.width - 1 && y <= surf.height - 1;
  };

  std::vector<const Tile*> sorted;
  for (const Tile* t : visible)
    if (t->has_max) sorted.push_back(t);
  std::sort(sorted.begin(), sorted.end(),
            [](const Tile* a, const Tile* b) { return a->max_m > b->max_m; });

  bool found = false;
  float best = 0.0f;
  GeoPoint best_at;
  for (const Tile* t : sorted) {
    if (found && best > t->max_m) break;  // nothing below can beat it
    if (on_screen(t->max_at)) {
      if (!found || t->max_m > best) {
        found = true;
        best = t->max_m;
        best_at = t->max_at;
      }
      break;  // this tile's own maximum is in view: done
    }
    // Its peak is off screen, so search the part of it that is not.
    const ElevationGrid& g = t->grid;
    for (int r = 0; r < g.height; ++r) {
      for (int c = 0; c < g.width; ++c) {
        const float e = g.At(r, c);
        if (std::isnan(e) || (found && e <= best)) continue;
        const GeoPoint p{g.bounds.ll.lat + r * g.LatStep(),
                         g.bounds.ll.lon + c * g.LonStep()};
        if (!on_screen(p)) continue;
        found = true;
        best = e;
        best_at = p;
      }
    }
  }
  if (!found) return Status::Ok();

  stats_.peak_drawn = true;
  stats_.peak_elev_m = best;
  stats_.peak = best_at;

  const FvColor peak_color = GetColor("peak_color", FvColor{0, 0, 0, 255});
  BuiltinSymbolLibrary lib;
  lib.SetColor(peak_color);
  ISymbolLibrary* previous = gd.symbols();
  gd.SetSymbols(&lib);
  PointSymbolStyle sym;
  sym.valid = true;
  Status s = gd.DrawSymbol(best_at, builtin_symbol::kTriangle, sym);
  gd.SetSymbols(previous);
  if (!with_label) return s;

  // FalconView's own string is "%.0f ft MSL", always feet; here it follows
  // the overlay's display unit, because an overlay whose altitude is in
  // metres should not label its peak in feet.
  const bool feet = GetInt("unit", kFeet) == kFeet;
  char text[64];
  std::snprintf(text, sizeof(text), "%lld %s MSL",
                static_cast<long long>(
                    std::llround(feet ? best / kMetersPerFoot : best)),
                feet ? "ft" : "m");

  LabelStyle label;
  label.valid = true;
  label.style.color = GetColor("label_color", FvColor{0, 0, 0, 255});
  label.style.size = GetDouble("label_size_px", 12.0);
  label.style.font_path = GetString("label_font");
  label.halo_width = 1.0;
  label.halo_color = GetColor("label_halo_color", FvColor{255, 255, 255, 220});
  label.placement = LabelPlacement::kPoint;
  // To the right of the marker and on its centre line, which is the
  // UTIL_ANCHOR_CENTER_LEFT the original asks for.
  label.dx = 10;
  label.dy = 4;
  const Status ls = gd.DrawLabel(best_at, text, label);
  return s.ok() ? ls : s;
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

Status TAMaskOverlay::OnDraw(const MapProjection& proj, ICanvas& canvas) {
  stats_ = DrawStats{};
  if (!proj.Ready()) return Status::Error(kInvalidArg, "projection not ready");
  if (source_ == nullptr) {
    stats_.no_source = true;
    return Status::Ok();
  }

  const double scale = ScaleDenominatorFor(proj);
  stats_.scale_denominator = scale;
  if (scale > GetDouble("display_threshold", 2000000.0)) {
    stats_.below_threshold = true;
    return Status::Ok();
  }

  bool invalidated = false;
  if (!tiling_.Adopt(proj, source_.get(), &invalidated)) return Status::Ok();
  if (invalidated) tiles_.clear();
  stats_.tile_deg = tiling_.tile_deg();
  stats_.sample_lat_deg = tiling_.sample_lat();
  stats_.sample_lon_deg = tiling_.sample_lon();

  const ClearanceLevels levels = Levels();
  stats_.altitude_m = AltitudeMeters();

  long long budget = GetInt("max_samples_per_draw", 1000000);
  std::vector<Tile*> visible;
  for (TileIndex index : tiling_.VisibleCells(proj)) {
    ++stats_.tiles_considered;
    Tile* t = TileAt(index, &budget);
    if (t != nullptr) visible.push_back(t);
  }

  Status first = Status::Ok();
  auto note = [&first](const Status& s) {
    if (!s.ok() && first.ok()) first = s;
  };

  // The fill is under everything else it draws: the outlines are the edges of
  // the bands and the peak marker sits on the ground the bands colour.
  if (GetBool("draw_mask", true) || GetBool("show_no_data_mask", true))
    note(DrawMask(proj, canvas, visible, levels));

  GeoDraw gd(proj, &canvas);
  if (GetBool("draw_contours", false))
    note(DrawContours(proj, gd, visible, levels));

  if (GetBool("show_peak", true)) {
    const bool label = GetBool("show_labels", true) &&
                       scale <= GetDouble("label_threshold", 500000.0);
    note(DrawPeak(proj, gd, visible, label));
  }

  EvictOutside(tiling_.KeepRect(proj));
  return first;
}

}  // namespace fv

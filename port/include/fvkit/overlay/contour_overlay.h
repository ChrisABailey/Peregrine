// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/overlay/contour_overlay.h — the Contour Lines overlay, ported from
// Applications/FalconView/Contour. Plan: port/contour-plan.md (C2 and C3).
// The tracing lives next door in fvkit/geo/terrain_contour.h; this file is
// everything around it — when to draw, what to sample, what to keep, and how
// it looks.
//
// WHAT CAME ACROSS FROM contour.cpp, and it is all in `OnDraw`:
//
//   * THE SCALE THRESHOLD. Contours over a 1:5 M chart are a smear, so the
//     original refuses to draw below `DisplayThreshold` (1:250 K), and has a
//     second, independent threshold for the labels. Both are properties here.
//   * THE FIXED GEOGRAPHIC TILE LATTICE. The screen is not the unit of work:
//     a lattice of whole fractions of a degree is, so a pan reuses what the
//     last frame traced and the lines do not writhe as the map moves. Tiles
//     share their edge POSTS, which is what makes a contour meet itself
//     across a tile boundary (FalconView asked its DTED server for one extra
//     post north and east to arrange the same thing). The lattice itself,
//     the sampling rule and the hysteresis below MOVED OUT of this file on
//     2026-09-01, when the TA mask overlay became their second consumer:
//     they are `fvkit/geo/elevation_tiling.h` now, unchanged.
//   * SAMPLE AT max(4 screen pixels, the native post spacing). Finer than the
//     posts invents terrain; finer than 4 px pays for ink nobody can see.
//     `IElevationSource::PostSpacing` was added for the second half of that.
//   * THE ONE-THIRD HYSTERESIS. Re-sampling on every zoom nudge would throw
//     the cache away continuously, so the sampling only moves when the
//     degrees-per-pixel has moved by more than a third.
//   * MAJOR AND MINOR AS LINE WEIGHT, ONE COLOUR, and labels on major lines
//     only, with the line BROKEN for the text rather than the text sitting on
//     top of it.
//
// WHAT DID NOT COME ACROSS:
//
//   * `DataSource` — a radio group choosing DTED level 1, 2 or 3 by hand. The
//     ported elevation source already prefers the finest cell it has at each
//     point, so the setting would only let a user ask for worse data.
//   * The `set_valid`/`get_valid` flag, the hourglass cursor, the six
//     registry reads per frame, and `prepare_for_draw`, whose entire body is
//     `if (force_redraw) force_redraw = false;`.
//   * `ThinningLevel`. It is stored, clamped to 1..10, written to the
//     registry, exposed in the property page — and never read. C5 gives the
//     idea a real implementation; this file does not pretend to one.
//
// THE ELEVATION SOURCE IS SET, NOT CONSTRUCTED, for the reason VectorMapOverlay
// documents: a type-registry factory takes no arguments, so a shell attaches
// the source after creating the overlay. With none attached this draws
// nothing at all, which is the honest thing for a terrain overlay on a
// machine with no terrain.

#ifndef FVKIT_OVERLAY_CONTOUR_OVERLAY_H_
#define FVKIT_OVERLAY_CONTOUR_OVERLAY_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/app/properties.h"
#include "fvkit/formats/source.h"
#include "fvkit/geo/elevation_tiling.h"
#include "fvkit/geo/terrain_contour.h"
#include "fvkit/overlay/overlay.h"

namespace fv {

class GeoDraw;        // fvkit/canvas/geo_draw.h
class LabelPlacer;    // fvkit/canvas/label_placer.h
struct GeoLineStyle;  // fvkit/canvas/geo_draw.h
struct LabelStyle;    // fvkit/vector/style.h

class ContourOverlay : public Overlay, public app::Properties {
 public:
  ContourOverlay();

  static const char kTypeId[];  // "fv.contour"

  // May be null (draws nothing). Replacing it drops every traced tile.
  void SetElevationSource(std::shared_ptr<IElevationSource> src);
  const std::shared_ptr<IElevationSource>& elevation_source() const {
    return source_;
  }

  Status OnDraw(const MapProjection& proj, ICanvas& canvas) override;

  app::Properties* AsProperties() override { return this; }
  const std::vector<app::PropertySpec>& Describe() const override;
  Status GetProperty(const std::string& key,
                     app::PropertyValue* out) const override;
  Status SetProperty(const std::string& key,
                     const app::PropertyValue& value) override;

  // The MINOR interval in metres — the one contours are actually traced at.
  // major_interval / divisions, converted from the display unit.
  //
  // METRES ARE THE TRUTH and the unit is a display choice, exactly as in the
  // original ("the unit stored in the registry ... is always in meters since
  // dted is stored in meters" — contour_pp.cpp). The deviation is only in
  // WHERE the conversion happens: FalconView converted in its dialog and
  // stored metres, so a registry holding 304.8 was a user who had typed 1000
  // feet. Here `major_interval` is in `interval_unit`s, because a person
  // editing peregrine.ini writes what they mean.
  double IntervalMeters() const;
  double MajorIntervalMeters() const;

  // Everything traced so far, dropped when the interval, the sampling or the
  // source changes. Not a picture cache: this is geography, so it survives a
  // pan, a rotation and a resize.
  void ClearCache();
  size_t cached_tiles() const { return tiles_.size(); }

  // --- diagnostics, for tests ---------------------------------------------
  // A contour overlay's failure modes are all counting failures — the wrong
  // number of levels, a tile traced twice, labels nobody could place — and a
  // pixel hash names none of them.
  struct DrawStats {
    bool below_threshold = false;  // refused by display_threshold
    bool no_source = false;
    int tiles_considered = 0;  // intersecting the viewport
    int tiles_traced = 0;      // of those, traced THIS draw (cache misses)
    int tiles_over_budget = 0;  // wanted tracing, refused by the sample budget
    int lines_drawn = 0;
    int major_lines = 0;
    int vertices = 0;         // traced vertices in the lines drawn
    int shaped_vertices = 0;  // vertices actually stroked, after C5's
                              // thinning and smoothing -- the ratio of the
                              // two is what smoothing costs in ink
    int labels_placed = 0;
    int labels_rejected = 0;   // wanted a label, could not fit one
    int labels_offscreen = 0;  // major line with no visible run to label
    long long samples = 0;  // elevation posts read this draw
    double scale_denominator = 0.0;
    double interval_m = 0.0;
    double sample_lat_deg = 0.0;
    double sample_lon_deg = 0.0;
    double tile_deg = 0.0;
  };
  const DrawStats& last_draw() const { return stats_; }

 private:
  struct Tile {
    GeoRect bounds;
    std::vector<ContourLine> lines;
  };

  // Traces the tile at lattice cell `index` if it is not already held.
  // Returns null when it could not be traced this frame.
  const Tile* TileAt(TileIndex index, long long* budget);

  // Draws one major contour WITH its elevation on it, the line broken for the
  // text. False means no label was placed (no room, too curved, or refused by
  // the placer) and nothing was drawn -- the caller then draws the plain line.
  bool DrawLabelledContour(const MapProjection& proj, GeoDraw& gd,
                           ICanvas& canvas, LabelPlacer& placer,
                           const ContourLine& line, const GeoLineStyle& style,
                           const LabelStyle& label_style);
  void EvictOutside(const GeoRect& keep);
  bool AdoptSampling(const MapProjection& proj);

  std::shared_ptr<IElevationSource> source_;
  std::vector<app::PropertyValue> values_;

  std::map<int64_t, Tile> tiles_;
  // The lattice and the sampling policy, shared with the TA mask overlay
  // (fvkit/geo/elevation_tiling.h). It holds the spacing and the tile size;
  // `tiles_` above holds what was traced at them.
  ElevationTiling tiling_;
  double traced_interval_m_ = 0.0;

  DrawStats stats_;
};

}  // namespace fv

#endif  // FVKIT_OVERLAY_CONTOUR_OVERLAY_H_

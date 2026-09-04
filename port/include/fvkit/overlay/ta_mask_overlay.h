// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/overlay/ta_mask_overlay.h — the Terrain Avoidance Mask overlay,
// ported from Applications/FalconView/TAMask. Plan: port/tamask-plan.md.
//
// An aircraft is at some altitude MSL; this colours the ground by how much
// room is left underneath it. Three named bands, tested from the top down:
//
//   warning   elev >= altitude - 100 ft   red
//   caution   elev >= altitude - 300 ft   yellow
//   OK        elev >= altitude - 500 ft   green
//   (below that: nothing, and a void post is magenta if asked for)
//
// The `>=` is FalconView's and matters: a post exactly on the warning level
// is a warning. `ConvertAltitudeToColor`, the mask loop and
// `TraceClearanceContours` all agree on it over there, and all three of them
// are this one classifier here.
//
// WHAT CAME ACROSS:
//
//   * THE BANDS, their default clearances and their colours, the 50% shading
//     the fill is blended at, and the separate no-data colour — which is
//     drawn whether or not the fill is, because a hole in the coverage under
//     an aircraft is the most important thing on the screen.
//   * NEAREST POST, NOT INTERPOLATED. The mask is blocky on purpose.
//     FalconView goes out of its way (`screen_ll.lon -= half_dted_lonpix`) to
//     align its blocks with the posts so that a mask square agrees with the
//     cursor elevation readout; rounding to the nearest post lands on the
//     same squares without the arithmetic.
//   * MASK ON, CONTOUR LINES OFF by default, the lines being the same three
//     levels as the fill (TraceElevationContoursAtLevels, plan TA2).
//   * TWO SCALE THRESHOLDS, 1:2 M and 1:500 K, and note they are COARSER than
//     the contour overlay's 1:250 K: a mask still reads at a scale where
//     contour lines are a smear.
//   * THE PEAK MARKER — the highest post in view, ONE only, with its
//     elevation. FalconView's own comment says multiple-equal-peak marking
//     was removed because flat ground "cause[s] a system near lockup"; the
//     disabled code is still in TAMask.cpp and stays disabled here.
//   * THE SENSITIVITY DEAD BAND (25 ft), so a jittering live altitude does
//     not repaint the map continuously.
//
// WHAT DID NOT, and the first two are the interesting ones:
//
//   * THE SCREEN-MASK ASSEMBLY. `draw_to_base_map` spends ~200 lines
//     concatenating per-tile byte masks into a pixmap aligned to a 0.2-degree
//     lattice, computing a sub-rectangle offset, memcpy-ing that rectangle
//     over the top of the same buffer, and handing the result to a stretching
//     blitter. All of it exists because the mask was built in DTED space and
//     had to reach screen space. This builds it IN SCREEN SPACE: the
//     projection is affine (fvkit/proj.h), so a scanline steps lat and lon by
//     constants and reads the post under each pixel. One buffer, one
//     DrawPixmap, no alignment arithmetic, and correct under rotation for
//     free.
//   * THE CACHED CLASSIFICATION (`m_ContourMask`). Caching the ANSWER means
//     every altitude change throws every tile away — `m_ContoursValid=false`
//     off nine `static` old-value comparisons, which are function-level
//     statics, so two TA mask overlays in one process would invalidate each
//     other's tiles. Here the ELEVATION is cached and the classification is
//     three comparisons per pixel, so a live altitude feed re-colours at no
//     cost at all. That is why `sensitivity` below damps a repaint request
//     and nothing more.
//   * `CMaskClipRgn` (294 lines) — a circular-wedge clip driven by the moving
//     map's bullseye, whose whole purpose is to make the raster path smaller
//     because the raster path was expensive. This one is a scanline over
//     pixels that are on the screen anyway. THE ONE DELIBERATE FEATURE
//     OMISSION in this port, said out loud.
//   * `DrawToVerticalDisplay`, whose body begins `return SUCCESS;` above
//     fifteen lines of unreachable code; `CTAMaskStatus` (571 lines of MFC
//     dialog); the property page; the `IXMLPrefMgr` reads; and the "KLUDGE"
//     block that writes six label settings to the registry on construction so
//     the contour code can read them back out. `app::Properties` is all of
//     that.
//   * `DataSource`, a hand-picked DTED level, for the reason contour_overlay.h
//     gives.
//   * `-32767` AS A NUMBER. The original compares against it in five places
//     and traces through it in a sixth. A void is NaN here (D4).
//
// The elevation source is SET, not constructed, exactly as ContourOverlay's
// is and for the same reason: a type-registry factory takes no arguments.
// With none attached this draws nothing, which is the honest thing for a
// terrain overlay on a machine with no terrain.

#ifndef FVKIT_OVERLAY_TA_MASK_OVERLAY_H_
#define FVKIT_OVERLAY_TA_MASK_OVERLAY_H_

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

class GeoDraw;  // fvkit/canvas/geo_draw.h

// What one elevation is, given an altitude and the three clearances. The
// order is the drawing order and the test order: highest band first.
enum class ClearanceBand {
  kNone = 0,   // below every band -- no ink
  kOk = 1,     // green
  kCaution = 2,  // yellow
  kWarn = 3,   // red
  kNoData = 4,  // a void post inside coverage -- magenta
};

// The three levels in METRES MSL, already resolved from an altitude and its
// clearances, plus whether each band is wanted at all. Metres because the
// posts are metres; feet is a display unit and stops at the property.
//
// A band that is not shown is not merely uncoloured: the band BELOW it takes
// its ground, which is FalconView's own behaviour and is the whole of what
// `OKColorIdx`/`CautionColorIdx`/`WarnColorIdx` do over there (each defaults
// to the next one down when its level is switched off). Turning off "caution"
// therefore makes the caution ground green, not blank.
struct ClearanceLevels {
  double warn_m = 0.0;
  double caution_m = 0.0;
  double ok_m = 0.0;
  bool show_warn = true;
  bool show_caution = true;
  bool show_ok = true;
};

// Which band an elevation falls in. NaN is kNoData; the caller decides
// whether a no-data post is inside coverage and worth colouring.
ClearanceBand ClassifyClearance(float elevation_m, const ClearanceLevels& l);

class TAMaskOverlay : public Overlay, public app::Properties {
 public:
  TAMaskOverlay();

  static const char kTypeId[];  // "fv.tamask"

  // May be null (draws nothing). Replacing it drops every sampled tile.
  void SetElevationSource(std::shared_ptr<IElevationSource> src);
  const std::shared_ptr<IElevationSource>& elevation_source() const {
    return source_;
  }

  // The automation seam: FalconView's `raw_UpdateAltitude`, minus the COM and
  // minus the bullseye. `altitude` is in the overlay's own display unit, the
  // way the property is, and MSL. Returns true when the altitude actually
  // moved — i.e. when it cleared the `sensitivity` dead band — which is a
  // shell's cue to ask for a repaint and nothing more: the cache does not
  // depend on the altitude, so a rejected update costs nothing either way.
  bool SetAltitude(double altitude);
  double Altitude() const;      // display units
  double AltitudeMeters() const;

  // The three band levels in metres MSL for the current altitude, clearances
  // and show flags — what the fill, the lines and the classifier all read.
  ClearanceLevels Levels() const;

  Status OnDraw(const MapProjection& proj, ICanvas& canvas) override;

  app::Properties* AsProperties() override { return this; }
  const std::vector<app::PropertySpec>& Describe() const override;
  Status GetProperty(const std::string& key,
                     app::PropertyValue* out) const override;
  Status SetProperty(const std::string& key,
                     const app::PropertyValue& value) override;

  // Every tile sampled so far. Elevation, not colour, so it survives a pan, a
  // rotation, a resize AND an altitude change.
  void ClearCache();
  size_t cached_tiles() const { return tiles_.size(); }

  // --- diagnostics, for tests ---------------------------------------------
  // Same reasoning as ContourOverlay::DrawStats: the failure modes here are
  // counting failures — a band nobody painted, a tile sampled twice, a peak
  // found off screen — and a pixel hash names none of them.
  struct DrawStats {
    bool below_threshold = false;
    bool no_source = false;
    int tiles_considered = 0;
    int tiles_sampled = 0;      // cache misses filled this draw
    int tiles_over_budget = 0;  // wanted sampling, refused by the budget
    int mask_pixels = 0;        // pixels the fill inked
    // Indexed by ClearanceBand, and counting every pixel the source could
    // SPEAK about: ground outside the coverage is in none of them, which is
    // why these need not add up to the surface.
    int band_pixels[5] = {0, 0, 0, 0, 0};
    int contour_lines = 0;
    int contour_levels = 0;  // distinct bands that got an outline
    int contour_vertices = 0;
    bool peak_drawn = false;
    double peak_elev_m = 0.0;
    GeoPoint peak;
    long long samples = 0;
    double scale_denominator = 0.0;
    double altitude_m = 0.0;
    double tile_deg = 0.0;
    double sample_lat_deg = 0.0;
    double sample_lon_deg = 0.0;
  };
  const DrawStats& last_draw() const { return stats_; }

 private:
  // A sampled tile. `has_data` is the reason this is not just a grid: see
  // OnDraw. `max_*` is FalconView's per-tile peak, kept for the same reason
  // it kept one — it is what makes the on-screen peak search stop early.
  struct Tile {
    GeoRect bounds;
    ElevationGrid grid;
    // One byte per post: did the SOURCE answer here? A NaN with `covered` set
    // is a void inside the terrain and gets the no-data colour; a NaN with it
    // clear is ground the source does not reach and gets nothing. The grid
    // itself cannot tell the two apart, and a lattice tile is not a DTED cell
    // -- it straddles the edge of the coverage routinely -- so a per-TILE
    // flag would paint a quarter of the screen magenta the moment an aircraft
    // approached the edge of its data. (Measured: it did, over the Georgia
    // test cell, before this existed.)
    std::vector<unsigned char> covered;
    bool has_data = false;
    float max_m = 0.0f;
    GeoPoint max_at;
    bool has_max = false;
    // The outlines, traced lazily and only when they are switched on, kept
    // beside the LEVELS they were traced at: a still aircraft re-uses them
    // and a climbing one re-traces, which is FalconView's behaviour without
    // its nine static old-value comparisons.
    std::vector<double> traced_levels;
    std::vector<ContourLine> lines;
  };

  Tile* TileAt(TileIndex index, long long* budget);
  void EvictOutside(const GeoRect& keep);

  // The raster half (plan TA4): one screen-sized RGBA buffer, one DrawPixmap.
  Status DrawMask(const MapProjection& proj, ICanvas& canvas,
                  const std::vector<Tile*>& visible,
                  const ClearanceLevels& levels);
  // The lines half: the same three levels, traced per tile and stroked.
  Status DrawContours(const MapProjection& proj, GeoDraw& gd,
                      const std::vector<Tile*>& visible,
                      const ClearanceLevels& levels);
  // The peak half (plan TA5), FalconView's algorithm.
  Status DrawPeak(const MapProjection& proj, GeoDraw& gd,
                  const std::vector<Tile*>& visible, bool with_label);

  std::shared_ptr<IElevationSource> source_;
  std::vector<app::PropertyValue> values_;

  std::map<int64_t, Tile> tiles_;
  ElevationTiling tiling_;

  DrawStats stats_;
};

}  // namespace fv

#endif  // FVKIT_OVERLAY_TA_MASK_OVERLAY_H_

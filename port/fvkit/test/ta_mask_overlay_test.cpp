// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The Terrain Avoidance Mask overlay (port/tamask-plan.md): the classifier,
// the bands, the raster, the altitude seam and the peak. The elevation
// sources here are analytic, so every count below is a fact about the
// overlay's own rules rather than about a DTED file that might not be on this
// machine — the same reasoning contour_overlay_test.cpp gives.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>

#include "fvkit/app/type_registry.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/overlay/ta_mask_overlay.h"
#include "fvkit/proj.h"

namespace {

using fv::ClearanceBand;
using fv::ClearanceLevels;
using fv::GeoPoint;
using fv::GeoRect;
using fv::Status;
using fv::TAMaskOverlay;

constexpr double kFt = 0.3048;

// A cone 3000 m high with its summit at (34, -84), falling 1 m every 3 m out,
// on DTED-1 posts. The same shape contour_overlay_test.cpp uses, so a reader
// comparing the two overlays is comparing them over the same hill.
class ConeElevation : public fv::IElevationSource {
 public:
  GeoRect Bounds() const override { return GeoRect{{33.0, -85.0}, {35.0, -83.0}}; }
  Status GetElevation(const GeoPoint& p, float* out) override {
    ++queries;
    const double dy = (p.lat - 34.0) * 111000.0;
    const double dx = (p.lon + 84.0) * 92000.0;
    *out = static_cast<float>(std::max(0.0, 3000.0 - std::hypot(dx, dy) / 3.0));
    return Status::Ok();
  }
  bool PostSpacing(const GeoPoint&, double* lat, double* lon) override {
    if (lat) *lat = 1.0 / 1200.0;
    if (lon) *lon = 1.0 / 1200.0;
    return true;
  }
  int queries = 0;
};

// Flat ground at 500 m with a square void in the middle of it — a hole INSIDE
// the coverage, which is what the no-data mask is for.
class HoleyElevation : public fv::IElevationSource {
 public:
  GeoRect Bounds() const override { return GeoRect{{33.0, -85.0}, {35.0, -83.0}}; }
  Status GetElevation(const GeoPoint& p, float* out) override {
    const bool in_hole = std::fabs(p.lat - 34.0) < 0.01 &&
                         std::fabs(p.lon + 84.0) < 0.01;
    *out = in_hole ? std::numeric_limits<float>::quiet_NaN() : 500.0f;
    return Status::Ok();
  }
  bool PostSpacing(const GeoPoint&, double* lat, double* lon) override {
    if (lat) *lat = 1.0 / 1200.0;
    if (lon) *lon = 1.0 / 1200.0;
    return true;
  }
};

// A source that REFUSES every point: no coverage at all, which must not be
// painted magenta.
class EmptyElevation : public fv::IElevationSource {
 public:
  GeoRect Bounds() const override { return GeoRect{{33.0, -85.0}, {35.0, -83.0}}; }
  Status GetElevation(const GeoPoint&, float*) override {
    return Status::Error(fv::kOutOfCoverage, "no data here");
  }
};

// Ground at 900 m west of -84 and NOTHING east of it: the edge of a coverage,
// which a lattice tile straddles routinely because a tile is not a DTED cell.
class HalfCoveredElevation : public fv::IElevationSource {
 public:
  GeoRect Bounds() const override { return GeoRect{{33.0, -85.0}, {35.0, -83.0}}; }
  Status GetElevation(const GeoPoint& p, float* out) override {
    if (p.lon > -84.0) return Status::Error(fv::kOutOfCoverage, "past the edge");
    *out = 900.0f;
    return Status::Ok();
  }
  bool PostSpacing(const GeoPoint&, double* lat, double* lon) override {
    if (lat) *lat = 1.0 / 1200.0;
    if (lon) *lon = 1.0 / 1200.0;
    return true;
  }
};

fv::MapProjection View(double scale, double lat = 34.0, double lon = -84.0,
                       int w = 320, int h = 240) {
  fv::MapProjection p;
  EXPECT_TRUE(p.SetSurfaceSize(w, h).ok());
  EXPECT_TRUE(p.SetCenter({lat, lon}).ok());
  EXPECT_TRUE(p.SetScale(scale).ok());
  return p;
}

// A system font, for the ONE test that is about the peak's label.
std::string SystemFont() {
  const char* candidates[] = {
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "/System/Library/Fonts/Supplemental/Courier New.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  };
  for (const char* f : candidates)
    if (FILE* fp = fopen(f, "rb")) {
      fclose(fp);
      return f;
    }
  return std::string();
}

// The peak's label needs a font, and a headless machine may have none — a
// canvas with no font is a kNotFound Status, not silent ink. Every test that
// is not ABOUT the label switches it off rather than depending on
// /System/Library being present.
void NoLabels(TAMaskOverlay& ovl) {
  EXPECT_TRUE(
      ovl.SetProperty("show_labels", fv::app::PropertyValue::Bool(false)).ok());
}

int BandPixels(const TAMaskOverlay& ovl, ClearanceBand b) {
  return ovl.last_draw().band_pixels[static_cast<int>(b)];
}

}  // namespace

// ---------------------------------------------------------------------------
// The classifier — every band edge, and what a switched-off band does
// ---------------------------------------------------------------------------

TEST(TAMaskClassifier, BandsAreTestedTopDownAndTheEdgeBelongsToTheHigherBand) {
  // An aircraft at 1000 m with 100/300/500 m of clearance.
  ClearanceLevels l;
  l.warn_m = 900.0;
  l.caution_m = 700.0;
  l.ok_m = 500.0;

  EXPECT_EQ(ClassifyClearance(1200.0f, l), ClearanceBand::kWarn);
  // EXACTLY on the level is the higher band -- FalconView's `>=`, in all
  // three of the places it spells this.
  EXPECT_EQ(ClassifyClearance(900.0f, l), ClearanceBand::kWarn);
  EXPECT_EQ(ClassifyClearance(899.999f, l), ClearanceBand::kCaution);
  EXPECT_EQ(ClassifyClearance(700.0f, l), ClearanceBand::kCaution);
  EXPECT_EQ(ClassifyClearance(699.999f, l), ClearanceBand::kOk);
  EXPECT_EQ(ClassifyClearance(500.0f, l), ClearanceBand::kOk);
  EXPECT_EQ(ClassifyClearance(499.999f, l), ClearanceBand::kNone);
  EXPECT_EQ(ClassifyClearance(std::numeric_limits<float>::quiet_NaN(), l),
            ClearanceBand::kNoData);
}

TEST(TAMaskClassifier, ASwitchedOffBandGivesItsGroundToTheOneBelow) {
  ClearanceLevels l;
  l.warn_m = 900.0;
  l.caution_m = 700.0;
  l.ok_m = 500.0;
  l.show_caution = false;

  // Ground that would be caution is now OK, not blank: FalconView's
  // CautionColorIdx defaults to OKColorIdx when the band is off.
  EXPECT_EQ(ClassifyClearance(800.0f, l), ClearanceBand::kOk);
  EXPECT_EQ(ClassifyClearance(950.0f, l), ClearanceBand::kWarn);

  l.show_ok = false;
  EXPECT_EQ(ClassifyClearance(800.0f, l), ClearanceBand::kNone);
}

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------

TEST(TAMaskOverlay, NoSourceDrawsNothing) {
  TAMaskOverlay ovl;
  fv::CpuCanvas canvas(320, 240);
  fv::MapProjection proj = View(500000.0);
  EXPECT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_TRUE(ovl.last_draw().no_source);
  EXPECT_EQ(ovl.last_draw().mask_pixels, 0);
  EXPECT_EQ(ovl.cached_tiles(), 0u);
}

TEST(TAMaskOverlay, RefusesSmallScalesAndSamplesNothingWhenItDoes) {
  TAMaskOverlay ovl;
  ovl.SetElevationSource(std::make_shared<ConeElevation>());
  NoLabels(ovl);
  fv::CpuCanvas canvas(320, 240);

  // The default threshold is 1:2 M -- coarser than the contour overlay's
  // 1:250 K, because a colour wash still reads where lines do not.
  fv::MapProjection wide = View(5000000.0);
  EXPECT_TRUE(ovl.OnDraw(wide, canvas).ok());
  EXPECT_TRUE(ovl.last_draw().below_threshold);
  EXPECT_EQ(ovl.last_draw().samples, 0);
  EXPECT_EQ(ovl.cached_tiles(), 0u);

  fv::MapProjection close = View(500000.0);
  EXPECT_TRUE(ovl.OnDraw(close, canvas).ok());
  EXPECT_FALSE(ovl.last_draw().below_threshold);
  EXPECT_GT(ovl.last_draw().mask_pixels, 0);
}

// ---------------------------------------------------------------------------
// The levels, which are the one piece of arithmetic a user gets wrong
// ---------------------------------------------------------------------------

TEST(TAMaskOverlay, LevelsAreTheAltitudeLessTheClearanceInMetres) {
  TAMaskOverlay ovl;
  // FalconView's defaults: 2500 ft, 100/300/500 ft of clearance.
  const ClearanceLevels l = ovl.Levels();
  EXPECT_NEAR(l.warn_m, (2500.0 - 100.0) * kFt, 1e-9);
  EXPECT_NEAR(l.caution_m, (2500.0 - 300.0) * kFt, 1e-9);
  EXPECT_NEAR(l.ok_m, (2500.0 - 500.0) * kFt, 1e-9);
  EXPECT_NEAR(ovl.AltitudeMeters(), 2500.0 * kFt, 1e-9);

  // Metres are the truth and feet is a display choice, so switching the unit
  // re-reads the SAME numbers as metres rather than converting them.
  ASSERT_TRUE(ovl.SetProperty("unit", fv::app::PropertyValue::Choice(1)).ok());
  const ClearanceLevels m = ovl.Levels();
  EXPECT_NEAR(m.warn_m, 2400.0, 1e-9);
  EXPECT_NEAR(ovl.AltitudeMeters(), 2500.0, 1e-9);
}

TEST(TAMaskOverlay, PropertiesAreCheckedNotTrusted) {
  TAMaskOverlay ovl;
  EXPECT_FALSE(ovl.SetProperty("shading",
                               fv::app::PropertyValue::Int(400)).ok());
  EXPECT_FALSE(ovl.SetProperty("no_such_key",
                               fv::app::PropertyValue::Bool(true)).ok());
  EXPECT_FALSE(ovl.SetProperty("unit", fv::app::PropertyValue::Choice(7)).ok());
  // The right type in the right range still works.
  EXPECT_TRUE(ovl.SetProperty("shading", fv::app::PropertyValue::Int(80)).ok());
  EXPECT_EQ(ovl.GetInt("shading", 50), 80);
}

// ---------------------------------------------------------------------------
// The raster
// ---------------------------------------------------------------------------

TEST(TAMaskOverlay, TheThreeBandsRingTheSummitInOrder) {
  TAMaskOverlay ovl;
  ovl.SetElevationSource(std::make_shared<ConeElevation>());
  NoLabels(ovl);
  fv::CpuCanvas canvas(320, 240);
  // Fly at 10,000 ft (3048 m) over a 3000 m cone. The clearances are opened
  // out from FalconView's 100/300/500 for one reason worth stating: this cone
  // falls 1 m every 3 m, so 200 feet of clearance is 180 metres of ground and
  // at any scale a mask is drawn at, the middle band would be thinner than a
  // pixel. The BANDS are what is under test, not the defaults.
  ASSERT_TRUE(
      ovl.SetProperty("altitude", fv::app::PropertyValue::Double(10000.0)).ok());
  ASSERT_TRUE(ovl.SetProperty("warn_clearance",
                              fv::app::PropertyValue::Double(500.0)).ok());
  ASSERT_TRUE(ovl.SetProperty("caution_clearance",
                              fv::app::PropertyValue::Double(3000.0)).ok());
  ASSERT_TRUE(ovl.SetProperty("ok_clearance",
                              fv::app::PropertyValue::Double(6000.0)).ok());
  // The peak marker is drawn ON the summit, so it would be the pixel read
  // below rather than the fill under it.
  ASSERT_TRUE(
      ovl.SetProperty("show_peak", fv::app::PropertyValue::Bool(false)).ok());
  fv::MapProjection proj = View(200000.0);
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());

  EXPECT_GT(BandPixels(ovl, ClearanceBand::kWarn), 0);
  EXPECT_GT(BandPixels(ovl, ClearanceBand::kCaution), 0);
  EXPECT_GT(BandPixels(ovl, ClearanceBand::kOk), 0);
  EXPECT_GT(BandPixels(ovl, ClearanceBand::kNone), 0);
  // A cone: each band is an annulus further out than the last, so each one is
  // wider on the ground than the one inside it.
  EXPECT_GT(BandPixels(ovl, ClearanceBand::kCaution),
            BandPixels(ovl, ClearanceBand::kWarn));
  EXPECT_GT(BandPixels(ovl, ClearanceBand::kOk),
            BandPixels(ovl, ClearanceBand::kCaution));
  EXPECT_EQ(ovl.last_draw().mask_pixels,
            BandPixels(ovl, ClearanceBand::kWarn) +
                BandPixels(ovl, ClearanceBand::kCaution) +
                BandPixels(ovl, ClearanceBand::kOk));

  // The summit pixel is at the centre of the surface and is red.
  const fv::PixelBuffer& b = canvas.Buffer();
  const unsigned char* px = b.Row(120) + 160 * 4;
  EXPECT_GT(px[0], px[1]);  // more red than green
  EXPECT_GT(px[0], px[2]);
}

TEST(TAMaskOverlay, ShadingIsTheAlphaTheFillIsBlendedAt) {
  auto draw = [](int shading) {
    TAMaskOverlay ovl;
    ovl.SetElevationSource(std::make_shared<ConeElevation>());
    NoLabels(ovl);
    fv::CpuCanvas canvas(320, 240);
    canvas.Clear(fv::FvColor{0, 0, 0, 255});
    EXPECT_TRUE(
        ovl.SetProperty("altitude", fv::app::PropertyValue::Double(10000.0))
            .ok());
    EXPECT_TRUE(ovl.SetProperty("warn_clearance",
                                fv::app::PropertyValue::Double(500.0)).ok());
    EXPECT_TRUE(
        ovl.SetProperty("shading", fv::app::PropertyValue::Int(shading)).ok());
    EXPECT_TRUE(ovl.SetProperty("show_peak",
                                fv::app::PropertyValue::Bool(false)).ok());
    fv::MapProjection proj = View(200000.0);
    EXPECT_TRUE(ovl.OnDraw(proj, canvas).ok());
    return static_cast<int>(*(canvas.Buffer().Row(120) + 160 * 4));
  };

  // Pure red over black at 50% is FalconView's 128 out of 255.
  EXPECT_EQ(draw(50), 128);
  EXPECT_EQ(draw(100), 255);
  EXPECT_EQ(draw(0), 0);
}

TEST(TAMaskOverlay, DrawMaskOffLeavesTheBandsUnpaintedButStillClassified) {
  TAMaskOverlay ovl;
  ovl.SetElevationSource(std::make_shared<ConeElevation>());
  NoLabels(ovl);
  fv::CpuCanvas canvas(320, 240);
  ASSERT_TRUE(
      ovl.SetProperty("altitude", fv::app::PropertyValue::Double(10000.0)).ok());
  ASSERT_TRUE(ovl.SetProperty("warn_clearance",
                              fv::app::PropertyValue::Double(500.0)).ok());
  ASSERT_TRUE(
      ovl.SetProperty("draw_mask", fv::app::PropertyValue::Bool(false)).ok());
  fv::MapProjection proj = View(200000.0);
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_EQ(ovl.last_draw().mask_pixels, 0);
  EXPECT_GT(BandPixels(ovl, ClearanceBand::kWarn), 0);
}

// ---------------------------------------------------------------------------
// No data — the port's one necessary departure from the original
// ---------------------------------------------------------------------------

TEST(TAMaskOverlay, AHoleInsideCoverageIsPaintedAndEmptyGroundIsNot) {
  {
    TAMaskOverlay ovl;
    ovl.SetElevationSource(std::make_shared<HoleyElevation>());
    NoLabels(ovl);
    fv::CpuCanvas canvas(320, 240);
    fv::MapProjection proj = View(200000.0);
    ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
    EXPECT_GT(BandPixels(ovl, ClearanceBand::kNoData), 0);
    EXPECT_GT(ovl.last_draw().mask_pixels, 0);
  }
  {
    // A source that covers nothing must not flood the screen magenta, which
    // is what a literal transcription of FalconView's mask loop would do:
    // over there a tile only existed if the coverage manager had listed it.
    TAMaskOverlay ovl;
    ovl.SetElevationSource(std::make_shared<EmptyElevation>());
    NoLabels(ovl);
    fv::CpuCanvas canvas(320, 240);
    fv::MapProjection proj = View(200000.0);
    ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
    EXPECT_EQ(ovl.last_draw().mask_pixels, 0);
    EXPECT_EQ(BandPixels(ovl, ClearanceBand::kNoData), 0);
    EXPECT_FALSE(ovl.last_draw().peak_drawn);
  }
}

TEST(TAMaskOverlay, TheEdgeOfACoverageIsNotAHoleInIt) {
  // THE BUG THIS TEST EXISTS FOR, found by drawing the Georgia DTED cell at
  // 1:500 K on 2026-09-01: a lattice tile is NOT a DTED cell, so a tile
  // straddling the edge of the data is PART covered. A per-tile "has data"
  // flag says yes, and the uncovered half then takes the no-data colour --
  // 85,000 magenta pixels, a quarter of that frame. The coverage mask is per
  // POST for this reason.
  TAMaskOverlay ovl;
  ovl.SetElevationSource(std::make_shared<HalfCoveredElevation>());
  NoLabels(ovl);
  fv::CpuCanvas canvas(320, 240);
  // Centred ON the edge, so the viewport is half data and half nothing.
  fv::MapProjection proj = View(200000.0, 34.0, -84.0);
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());

  EXPECT_EQ(BandPixels(ovl, ClearanceBand::kNoData), 0);
  EXPECT_GT(ovl.last_draw().mask_pixels, 0);
  // Only the covered half is inked, to within a column of rounding.
  const fv::PixelSize size = proj.SurfaceSize();
  const double total = static_cast<double>(size.width) * size.height;
  EXPECT_LT(ovl.last_draw().mask_pixels, total * 0.55);
  EXPECT_GT(ovl.last_draw().mask_pixels, total * 0.45);
}

// ---------------------------------------------------------------------------
// The altitude — the thing this overlay has and no other does
// ---------------------------------------------------------------------------

TEST(TAMaskOverlay, AnAltitudeChangeRecoloursWithoutReadingAnyTerrain) {
  TAMaskOverlay ovl;
  auto src = std::make_shared<ConeElevation>();
  ovl.SetElevationSource(src);
  NoLabels(ovl);
  fv::CpuCanvas canvas(320, 240);
  fv::MapProjection proj = View(200000.0);

  ASSERT_TRUE(
      ovl.SetProperty("altitude", fv::app::PropertyValue::Double(10000.0)).ok());
  ASSERT_TRUE(ovl.SetProperty("warn_clearance",
                              fv::app::PropertyValue::Double(500.0)).ok());
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  const int sampled = ovl.last_draw().tiles_sampled;
  const int warn_low = BandPixels(ovl, ClearanceBand::kWarn);
  const int queries = src->queries;
  EXPECT_GT(sampled, 0);

  // Climb 5000 feet. FalconView sets m_ContoursValid = false here and rebuilds
  // every tile's byte mask; we cache the ELEVATION, so not one post is re-read.
  ASSERT_TRUE(ovl.SetAltitude(15000.0));
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_EQ(ovl.last_draw().tiles_sampled, 0);
  EXPECT_EQ(ovl.last_draw().samples, 0);
  EXPECT_EQ(src->queries, queries);
  // Higher up, less of the cone is inside the warning band.
  EXPECT_LT(BandPixels(ovl, ClearanceBand::kWarn), warn_low);
}

TEST(TAMaskOverlay, SensitivityIsADeadBandOnTheAltitudeFeed) {
  TAMaskOverlay ovl;
  EXPECT_EQ(ovl.Altitude(), 2500.0);
  // Inside the 25 ft dead band: refused, and the altitude does not creep.
  EXPECT_FALSE(ovl.SetAltitude(2510.0));
  EXPECT_EQ(ovl.Altitude(), 2500.0);
  EXPECT_FALSE(ovl.SetAltitude(2524.0));
  EXPECT_EQ(ovl.Altitude(), 2500.0);
  // Outside it: taken exactly, not rounded to the band.
  EXPECT_TRUE(ovl.SetAltitude(2600.0));
  EXPECT_EQ(ovl.Altitude(), 2600.0);

  ASSERT_TRUE(
      ovl.SetProperty("sensitivity", fv::app::PropertyValue::Double(0.0)).ok());
  EXPECT_TRUE(ovl.SetAltitude(2600.5));
  EXPECT_EQ(ovl.Altitude(), 2600.5);
}

// ---------------------------------------------------------------------------
// The cache
// ---------------------------------------------------------------------------

TEST(TAMaskOverlay, APanReusesSampledTiles) {
  TAMaskOverlay ovl;
  ovl.SetElevationSource(std::make_shared<ConeElevation>());
  NoLabels(ovl);
  fv::CpuCanvas canvas(320, 240);

  fv::MapProjection a = View(200000.0, 34.0, -84.0);
  ASSERT_TRUE(ovl.OnDraw(a, canvas).ok());
  const int first = ovl.last_draw().tiles_sampled;
  ASSERT_GT(first, 0);

  // The same view again: everything is a cache hit.
  ASSERT_TRUE(ovl.OnDraw(a, canvas).ok());
  EXPECT_EQ(ovl.last_draw().tiles_sampled, 0);
  EXPECT_GT(ovl.last_draw().tiles_considered, 0);

  // A pan of a fraction of a tile touches at most one new lattice cell per
  // axis; it does not resample what it already had.
  fv::MapProjection b = View(200000.0, 34.005, -84.005);
  ASSERT_TRUE(ovl.OnDraw(b, canvas).ok());
  EXPECT_LT(ovl.last_draw().tiles_sampled, first + 1);
}

TEST(TAMaskOverlay, ReplacingTheSourceDropsEverything) {
  TAMaskOverlay ovl;
  ovl.SetElevationSource(std::make_shared<ConeElevation>());
  NoLabels(ovl);
  fv::CpuCanvas canvas(320, 240);
  fv::MapProjection proj = View(500000.0);
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  ASSERT_GT(ovl.cached_tiles(), 0u);
  ovl.SetElevationSource(std::make_shared<HoleyElevation>());
  NoLabels(ovl);
  EXPECT_EQ(ovl.cached_tiles(), 0u);
}

// ---------------------------------------------------------------------------
// The outlines
// ---------------------------------------------------------------------------

TEST(TAMaskOverlay, OutlinesAreOffByDefaultAndTraceTheSameThreeLevels) {
  TAMaskOverlay ovl;
  ovl.SetElevationSource(std::make_shared<ConeElevation>());
  NoLabels(ovl);
  fv::CpuCanvas canvas(320, 240);
  ASSERT_TRUE(
      ovl.SetProperty("altitude", fv::app::PropertyValue::Double(10000.0)).ok());
  ASSERT_TRUE(ovl.SetProperty("warn_clearance",
                              fv::app::PropertyValue::Double(500.0)).ok());
  fv::MapProjection proj = View(200000.0);

  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_EQ(ovl.last_draw().contour_lines, 0);  // FalconView's default

  ASSERT_TRUE(
      ovl.SetProperty("draw_contours", fv::app::PropertyValue::Bool(true)).ok());
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  // Three band edges, and LEVELS rather than lines is the count that means
  // something: each ring is cut where it crosses a lattice tile, so a cone
  // under four tiles produces four arcs per level and not one ring.
  EXPECT_EQ(ovl.last_draw().contour_levels, 3);
  EXPECT_GT(ovl.last_draw().contour_lines, 0);
  EXPECT_GT(ovl.last_draw().contour_vertices, 0);
  const int all_bands = ovl.last_draw().contour_lines;

  // Two bands switched off: the tracer is asked for one level rather than
  // three, so a third of the ink and a third of the work.
  ASSERT_TRUE(ovl.SetProperty("show_caution_level",
                              fv::app::PropertyValue::Bool(false)).ok());
  ASSERT_TRUE(
      ovl.SetProperty("show_ok_level", fv::app::PropertyValue::Bool(false)).ok());
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_EQ(ovl.last_draw().contour_levels, 1);
  EXPECT_LT(ovl.last_draw().contour_lines, all_bands);
}

// ---------------------------------------------------------------------------
// The peak (TA5)
// ---------------------------------------------------------------------------

TEST(TAMaskOverlay, ThePeakIsTheHighestPostInVIEWAndMovesWithTheMap) {
  TAMaskOverlay ovl;
  ovl.SetElevationSource(std::make_shared<ConeElevation>());
  NoLabels(ovl);
  fv::CpuCanvas canvas(320, 240);
  fv::MapProjection over = View(500000.0, 34.0, -84.0);
  ASSERT_TRUE(ovl.OnDraw(over, canvas).ok());
  ASSERT_TRUE(ovl.last_draw().peak_drawn);
  // The summit, to within a post.
  EXPECT_NEAR(ovl.last_draw().peak.lat, 34.0, 0.01);
  EXPECT_NEAR(ovl.last_draw().peak.lon, -84.0, 0.01);
  EXPECT_NEAR(ovl.last_draw().peak_elev_m, 3000.0, 60.0);

  // Pan off the summit: the marker does not stay behind on ground that is no
  // longer on screen, and the elevation it reports drops.
  fv::MapProjection aside = View(500000.0, 34.4, -84.0);
  ASSERT_TRUE(ovl.OnDraw(aside, canvas).ok());
  ASSERT_TRUE(ovl.last_draw().peak_drawn);
  EXPECT_GT(ovl.last_draw().peak.lat, 34.05);
  EXPECT_LT(ovl.last_draw().peak_elev_m, 3000.0);
}

TEST(TAMaskOverlay, ThePeakCarriesItsElevationInTheDisplayUnit) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no system font";
  TAMaskOverlay ovl;
  ovl.SetElevationSource(std::make_shared<ConeElevation>());
  fv::CpuCanvas canvas(320, 240);
  ASSERT_TRUE(canvas.SetDefaultFont(font).ok());
  fv::MapProjection proj = View(200000.0);
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_TRUE(ovl.last_draw().peak_drawn);

  // A canvas with NO font is a Status, not silent ink: the marker is still
  // stamped and the frame still says what went wrong.
  fv::CpuCanvas fontless(320, 240);
  TAMaskOverlay other;
  other.SetElevationSource(std::make_shared<ConeElevation>());
  const Status s = other.OnDraw(proj, fontless);
  EXPECT_FALSE(s.ok());
  EXPECT_TRUE(other.last_draw().peak_drawn);
  EXPECT_GT(other.last_draw().mask_pixels, 0);
}

TEST(TAMaskOverlay, ThePeakCanBeSwitchedOff) {
  TAMaskOverlay ovl;
  ovl.SetElevationSource(std::make_shared<ConeElevation>());
  NoLabels(ovl);
  fv::CpuCanvas canvas(320, 240);
  ASSERT_TRUE(
      ovl.SetProperty("show_peak", fv::app::PropertyValue::Bool(false)).ok());
  fv::MapProjection proj = View(500000.0);
  ASSERT_TRUE(ovl.OnDraw(proj, canvas).ok());
  EXPECT_FALSE(ovl.last_draw().peak_drawn);
}

// ---------------------------------------------------------------------------
// The registry
// ---------------------------------------------------------------------------

TEST(TAMaskOverlay, IsARegisteredStaticType) {
  fv::app::OverlayTypeRegistry registry;
  ASSERT_TRUE(fv::app::RegisterBuiltinOverlayTypes(registry).ok());
  const fv::app::OverlayTypeDesc* desc = registry.Find(TAMaskOverlay::kTypeId);
  ASSERT_NE(desc, nullptr);
  EXPECT_EQ(desc->display_name, "Terrain Avoidance Mask");
  EXPECT_TRUE(registry.IsStatic(TAMaskOverlay::kTypeId));
  EXPECT_FALSE(desc->restore_at_startup);
  // Above the contours (880), which describe the same ground with lines, and
  // under the graticule (900).
  const fv::app::OverlayTypeDesc* contour = registry.Find("fv.contour");
  ASSERT_NE(contour, nullptr);
  EXPECT_GT(desc->default_display_order, contour->default_display_order);
  EXPECT_LT(desc->default_display_order, 900);

  std::shared_ptr<fv::Overlay> made = desc->factory();
  ASSERT_NE(made, nullptr);
  EXPECT_NE(made->AsProperties(), nullptr);
  // Its settings section is [tamask], so peregrine.ini reaches it with no
  // shell change at all (OverlaySession::Instantiate applies the prefix).
  EXPECT_EQ(fv::app::SettingsPrefixForTypeId(TAMaskOverlay::kTypeId),
            "tamask.");
}

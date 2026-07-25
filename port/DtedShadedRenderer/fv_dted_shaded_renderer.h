// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_dted_shaded_renderer.h — portable facade over CDtedReader's shaded-relief
// engine (fvw_core/MapDataServer/DtedMapServer/renderer/DtedReader.cpp).
//
// This is the headless replacement for the COM/GDI-bound DTEDRenderer (disp.cpp,
// which stays Windows-only): it takes the same knobs — display mode, light
// direction / sun angle / time-of-day, contour, elevation/slope/colour bands —
// but instead of pushing pixels through IGraphicsContext::PutPixmap it renders
// one open cell into a top-down RGBA8 PixelBuffer. The non-COM lighting helpers
// (convert_to_cartesian, the default NW sun, the SLAC time-shading hook) are
// reproduced here from disp.cpp; SLAC's astronomy is replaced by the NOAA
// solar-position function (fv_solar_position.h).
//
// The rendered image is (image_width-1) x (image_height-1): the Windows
// renderer drops the north row and east column of posts (they overlap the
// neighbouring cell), and this reproduces that trim so cells tile seamlessly.
// PixelToGeo/GeoToPixel are the exact equal-arc transforms disp.cpp used.
//
// CDtedReader (with its Win32 shims) is kept behind a pimpl so consumers do not
// inherit fv_compat's macros (min/max/TRUE/FALSE/...).

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fv_dted_enums.h"
#include "fvkit/geo.h"
#include "fvkit/raster.h"

namespace fv {

class DtedShadedRenderer {
 public:
  DtedShadedRenderer();
  ~DtedShadedRenderer();
  DtedShadedRenderer(const DtedShadedRenderer&) = delete;
  DtedShadedRenderer& operator=(const DtedShadedRenderer&) = delete;

  Status Open(const std::string& path);
  void Close();
  bool IsOpen() const;

  // Rendered-image geometry (see the file header on the -1 trim).
  int RenderedWidth() const;
  int RenderedHeight() const;
  GeoRect ImageBounds() const;
  // Equal-arc transforms; pixel (0,0) = centre of the top-left rendered pixel.
  bool PixelToGeo(double px, double py, GeoPoint* p) const;
  bool GeoToPixel(const GeoPoint& p, double* px, double* py) const;

  // --- Parameters (defaults match FalconView: rendered colour, NW sun az 315
  // / alt 45, exaggeration 1, contours off, flat-is-black). ---
  void SetDisplayMode(DtedDisplayMode mode);
  DtedDisplayMode GetDisplayMode() const;
  void SetContourLinesOn(bool on);
  void SetContourIntervalFeet(double feet);
  void SetContourColor(int red, int green, int blue);
  void SetFlatIsBlack(bool black);
  // Feet breakpoints (elevation) / percent breakpoints (slope); the count is
  // clamped to the engine's 5-breakpoint maximum, as in FalconView. When no
  // elevation bands are set, FalconView's own defaults
  // (2500/5000/7500/10000/12500 ft) are applied — CDtedReader's constructor
  // defaults are unusable (they skip the feet->metres conversion; see the .cpp).
  void SetElevationBands(const std::vector<int>& feet);
  void SetColorBands(const std::vector<int>& red, const std::vector<int>& green,
                     const std::vector<int>& blue);
  void SetSlopeBands(const std::vector<double>& percent);

  // --- Lighting: choose exactly one; the last call wins. ---
  void SetLightDirection(double x, double y, double z);
  void SetSunAzimuthElevation(double azimuth_deg, double elevation_deg);
  // Per-cell sun from date+cell-centre (replaces SLAC). Sets exaggeration 3.0
  // like disp.cpp so low/overhead sun does not wash the map to black/white.
  void SetTimeShadingUtc(int year, int month, int day, double hours_utc);
  void ClearTimeShading();
  void SetExaggeration(float factor);

  // Renders the whole open cell into a top-down RGBA8 PixelBuffer.
  Status Render(PixelBuffer* out);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fv

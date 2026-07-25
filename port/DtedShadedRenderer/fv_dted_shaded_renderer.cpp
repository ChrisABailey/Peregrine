// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::DtedShadedRenderer implementation — see fv_dted_shaded_renderer.h.
// Includes DtedReader.h (and thus fv_compat's Win32 shims) inside this TU only.

#include "fv_dted_shaded_renderer.h"

#include <cmath>

#include "DtedReader.h"
#include "fv_solar_position.h"

namespace fv {

namespace {

// disp.cpp's default sun: north-west, 45 deg above the horizon.
constexpr double kDefaultAzimuth = 315.0;
constexpr double kDefaultAltitude = 45.0;

// FalconView's default elevation bands, in FEET — the same array
// CDtedRenderOptions::InitElevationBands (fvw_core/DtedMapOptions) pushes into
// the renderer at startup, and the values DTEDOptDlg shows as
// "5/2500/5000/7500/10000/12500".
//
// These MUST be applied. CDtedReader's constructor seeds m_elev_breakpts with
// the same five numbers but WITHOUT the feet->metres conversion that its
// set_elevation_bands() setter performs, so the raw defaults act as metres:
// band 0 then runs to 2500 m (8200 ft) and every CONUS elevation lands in it,
// collapsing the colour ramp to one band (a flat green map with only the
// shading ramp visible). On Windows the options page always calls the setter,
// so the bad constructor defaults never reach the screen; headless, nothing
// did until this facade started passing them.
const int kDefaultElevationBandsFeet[] = {2500, 5000, 7500, 10000, 12500};

enum LightMode { kDefaultNW, kExplicitVector, kSunAngle, kTimeShading };

// Verbatim from DTEDRenderer::convert_to_cartesian (disp.cpp) — NOT COM/GDI, so
// it moves into the portable facade unchanged (same 3.14159 constant, same
// quadrant branching, same z = -tan(elevation)). Azimuth is degrees clockwise
// from north, elevation degrees above the horizon.
void ConvertToCartesian(double azimuth, double elevation, double& x, double& y,
                        double& z) {
  if (azimuth == -999.0 && elevation == -999.0) {
    x = 1;
    y = -1;
    z = -1;
    return;
  }
  if (azimuth < 0) {
    while ((azimuth += 360) < 0) {
    }
  } else if (azimuth >= 360) {
    while ((azimuth -= 360) >= 360) {
    }
  }

  int sign = (azimuth >= 180) ? 1 : -1;
  double test_dir = (azimuth >= 180) ? azimuth - 180 : azimuth;

  if (test_dir >= 45 && test_dir <= 135) {
    x = (azimuth > 180) ? 1 : -1;
    y = sign / tan(azimuth * 3.14159 / 180);
  } else {
    x = tan(azimuth * 3.14159 / 180);
    y = (azimuth < 45 || azimuth > 270) ? -1 : 1;
  }

  z = -(tan(elevation * 1.7453292519943295e-2));  // elevation * pi/180
}

}  // namespace

struct DtedShadedRenderer::Impl {
  CDtedReader reader;

  // cached georef (valid while open)
  bool open = false;
  int img_w = 0, img_h = 0;
  double nw_lat = 0, nw_lon = 0, dpp_lat = 0, dpp_lon = 0;

  // parameters
  DtedDisplayMode display_mode = kDtedRenderedColor;
  bool contour_on = false;
  bool contour_interval_set = false;
  double contour_interval_ft = 0;
  bool contour_color_set = false;
  int contour_r = 0, contour_g = 0, contour_b = 255;
  bool flat_is_black = true;
  float exaggeration = 1.0f;

  bool elev_bands_set = false;
  std::vector<int> elev_bands;
  bool color_bands_set = false;
  std::vector<long> band_r, band_g, band_b;
  bool slope_bands_set = false;
  std::vector<double> slope_bands;

  LightMode light_mode = kDefaultNW;
  double lx = 0, ly = 0, lz = 0;   // explicit vector
  double sun_az = 0, sun_alt = 0;  // sun angle
  int ts_year = 0, ts_month = 0, ts_day = 0;
  double ts_hours = 0;
};

DtedShadedRenderer::DtedShadedRenderer() : impl_(new Impl) {}
DtedShadedRenderer::~DtedShadedRenderer() = default;

Status DtedShadedRenderer::Open(const std::string& path) {
  Close();
  if (impl_->reader.open(path) != DTED_READER_SUCCESS)
    return Status::Error(kIoError, impl_->reader.get_error_message());

  impl_->img_w = impl_->reader.get_image_width();
  impl_->img_h = impl_->reader.get_image_height();
  if (impl_->img_w < 2 || impl_->img_h < 2) {
    impl_->reader.close();
    return Status::Error(kUnsupported, "DTED cell too small to render");
  }
  double sw_lat, sw_lon, nw_lat, nw_lon, ne_lat, ne_lon, se_lat, se_lon;
  impl_->reader.get_image_bounding_rectangle(sw_lat, sw_lon, nw_lat, nw_lon,
                                             ne_lat, ne_lon, se_lat, se_lon);
  impl_->reader.get_degrees_per_pixel(impl_->dpp_lat, impl_->dpp_lon);
  impl_->nw_lat = nw_lat;
  impl_->nw_lon = nw_lon;
  impl_->open = true;
  return Status::Ok();
}

void DtedShadedRenderer::Close() {
  if (impl_->open) impl_->reader.close();
  impl_->open = false;
}

bool DtedShadedRenderer::IsOpen() const { return impl_->open; }

int DtedShadedRenderer::RenderedWidth() const {
  return impl_->open ? impl_->img_w - 1 : 0;
}
int DtedShadedRenderer::RenderedHeight() const {
  return impl_->open ? impl_->img_h - 1 : 0;
}

GeoRect DtedShadedRenderer::ImageBounds() const {
  if (!impl_->open) return GeoRect{};
  const double north = impl_->nw_lat - impl_->dpp_lat;
  const double south = impl_->nw_lat - (impl_->img_h - 1) * impl_->dpp_lat;
  const double west = impl_->nw_lon;
  const double east = impl_->nw_lon + (impl_->img_w - 2) * impl_->dpp_lon;
  return GeoRect{{south, west}, {north, east}};
}

bool DtedShadedRenderer::PixelToGeo(double px, double py, GeoPoint* p) const {
  if (!impl_->open || p == nullptr) return false;
  p->lon = impl_->nw_lon + px * impl_->dpp_lon;
  p->lat = impl_->nw_lat - (1.0 + py) * impl_->dpp_lat;
  return true;
}

bool DtedShadedRenderer::GeoToPixel(const GeoPoint& p, double* px,
                                    double* py) const {
  if (!impl_->open || px == nullptr || py == nullptr) return false;
  *px = (p.lon - impl_->nw_lon) / impl_->dpp_lon;
  *py = (impl_->nw_lat - p.lat) / impl_->dpp_lat - 1.0;
  return true;
}

void DtedShadedRenderer::SetDisplayMode(DtedDisplayMode mode) {
  impl_->display_mode = mode;
}
DtedDisplayMode DtedShadedRenderer::GetDisplayMode() const {
  return impl_->display_mode;
}
void DtedShadedRenderer::SetContourLinesOn(bool on) { impl_->contour_on = on; }
void DtedShadedRenderer::SetContourIntervalFeet(double feet) {
  impl_->contour_interval_set = true;
  impl_->contour_interval_ft = feet;
}
void DtedShadedRenderer::SetContourColor(int r, int g, int b) {
  impl_->contour_color_set = true;
  impl_->contour_r = r;
  impl_->contour_g = g;
  impl_->contour_b = b;
}
void DtedShadedRenderer::SetFlatIsBlack(bool black) {
  impl_->flat_is_black = black;
}
void DtedShadedRenderer::SetElevationBands(const std::vector<int>& feet) {
  impl_->elev_bands_set = true;
  impl_->elev_bands = feet;
}
void DtedShadedRenderer::SetColorBands(const std::vector<int>& red,
                                       const std::vector<int>& green,
                                       const std::vector<int>& blue) {
  impl_->color_bands_set = true;
  impl_->band_r.assign(red.begin(), red.end());
  impl_->band_g.assign(green.begin(), green.end());
  impl_->band_b.assign(blue.begin(), blue.end());
}
void DtedShadedRenderer::SetSlopeBands(const std::vector<double>& percent) {
  impl_->slope_bands_set = true;
  impl_->slope_bands = percent;
}

void DtedShadedRenderer::SetLightDirection(double x, double y, double z) {
  impl_->light_mode = kExplicitVector;
  impl_->lx = x;
  impl_->ly = y;
  impl_->lz = z;
}
void DtedShadedRenderer::SetSunAzimuthElevation(double az, double alt) {
  impl_->light_mode = kSunAngle;
  impl_->sun_az = az;
  impl_->sun_alt = alt;
}
void DtedShadedRenderer::SetTimeShadingUtc(int year, int month, int day,
                                           double hours_utc) {
  impl_->light_mode = kTimeShading;
  impl_->ts_year = year;
  impl_->ts_month = month;
  impl_->ts_day = day;
  impl_->ts_hours = hours_utc;
}
void DtedShadedRenderer::ClearTimeShading() { impl_->light_mode = kDefaultNW; }
void DtedShadedRenderer::SetExaggeration(float factor) {
  impl_->exaggeration = factor;
}

Status DtedShadedRenderer::Render(PixelBuffer* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "out is null");
  if (!impl_->open) return Status::Error(kInvalidArg, "no cell open");
  CDtedReader& r = impl_->reader;

  // --- apply parameters (mirrors DTEDRenderer::display's setup block) ---
  r.set_contour_width(1);
  r.set_contour_color_mode(0);
  r.set_contour_lines_on(impl_->contour_on ? TRUE : FALSE);
  if (impl_->contour_interval_set)
    r.set_contour_interval(static_cast<float>(impl_->contour_interval_ft));
  if (impl_->contour_color_set)
    r.set_contour_color(RGB(impl_->contour_r, impl_->contour_g, impl_->contour_b));
  r.set_flat_is_black(impl_->flat_is_black);
  r.set_display_mode(static_cast<unsigned short>(impl_->display_mode >> 1));
  r.set_color_mode((impl_->display_mode & 1) != 0);

  // Always go through set_elevation_bands (which converts feet->metres); see
  // kDefaultElevationBandsFeet on why the reader's own defaults are unusable.
  if (impl_->elev_bands_set && !impl_->elev_bands.empty()) {
    r.set_elevation_bands(static_cast<int>(impl_->elev_bands.size()),
                          impl_->elev_bands.data());
  } else {
    std::vector<int> feet(std::begin(kDefaultElevationBandsFeet),
                          std::end(kDefaultElevationBandsFeet));
    r.set_elevation_bands(static_cast<int>(feet.size()), feet.data());
  }
  if (impl_->color_bands_set && !impl_->band_r.empty())
    r.set_color_bands(static_cast<int>(impl_->band_r.size()),
                      impl_->band_r.data(), impl_->band_g.data(),
                      impl_->band_b.data());
  if (impl_->slope_bands_set && !impl_->slope_bands.empty())
    r.set_slope_bands(static_cast<int>(impl_->slope_bands.size()),
                      impl_->slope_bands.data());

  // --- lighting ---
  double x, y, z;
  switch (impl_->light_mode) {
    case kExplicitVector:
      r.set_light_direction(impl_->lx, impl_->ly, impl_->lz);
      r.set_exaggeration_factor(impl_->exaggeration);
      break;
    case kSunAngle:
      ConvertToCartesian(impl_->sun_az, impl_->sun_alt, x, y, z);
      r.set_light_direction(x, y, z);
      r.set_exaggeration_factor(impl_->exaggeration);
      break;
    case kTimeShading: {
      const GeoRect b = ImageBounds();
      const double clat = (b.ll.lat + b.ur.lat) / 2;
      const double clon = (b.ll.lon + b.ur.lon) / 2;
      SolarPosition sun = SolarAzimuthElevation(impl_->ts_year, impl_->ts_month,
                                                impl_->ts_day, impl_->ts_hours,
                                                clat, clon);
      ConvertToCartesian(sun.azimuth_deg, sun.elevation_deg, x, y, z);
      r.set_light_direction(x, y, z);
      r.set_exaggeration_factor(3.0f);  // disp.cpp: keep low/overhead sun legible
      break;
    }
    case kDefaultNW:
    default:
      ConvertToCartesian(kDefaultAzimuth, kDefaultAltitude, x, y, z);
      r.set_light_direction(x, y, z);
      r.set_exaggeration_factor(impl_->exaggeration);
      break;
  }

  r.setup_color_table();
  unsigned char color_table[DTED_READER_NUM_COLORS][3];
  r.get_color_table(color_table);

  const int W = impl_->img_w, H = impl_->img_h;
  const int sub_w = W - 1, sub_h = H - 1;
  std::vector<unsigned char> idx(static_cast<size_t>(sub_w) * sub_h);
  // get_subimage renders hpix [0, W-2] x vpix [1, H-1], bottom (south) row
  // first — exactly the extent disp.cpp trims to for seamless tiling.
  if (r.get_subimage(0, 1, W - 2, H - 1, idx.data()) != DTED_READER_SUCCESS)
    return Status::Error(kIoError, r.get_error_message());

  *out = PixelBuffer(sub_w, sub_h);
  for (int oy = 0; oy < sub_h; ++oy) {
    // flip: PixelBuffer is top-down (row 0 = north); the index image is
    // bottom-up (row 0 = south).
    const unsigned char* srow =
        idx.data() + static_cast<size_t>(sub_h - 1 - oy) * sub_w;
    unsigned char* dst = out->Row(oy);
    for (int ox = 0; ox < sub_w; ++ox) {
      const unsigned char ci = srow[ox];
      dst[4 * ox + 0] = color_table[ci][0];
      dst[4 * ox + 1] = color_table[ci][1];
      dst[4 * ox + 2] = color_table[ci][2];
      dst[4 * ox + 3] = 255;
    }
  }
  return Status::Ok();
}

}  // namespace fv

// Copyright (c) 1994-2009 Georgia Tech Research Corporation, Atlanta, GA
// This file is part of FalconView(tm).

// FalconView(tm) is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// FalconView(tm) is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public License
// along with FalconView(tm).  If not, see <http://www.gnu.org/licenses/>.

// FalconView(tm) is a trademark of Georgia Tech Research Corporation.

// Copyright (c) 1994-2009 Georgia Tech Research Corporation, Atlanta, GA
// This file is part of FalconView(tm).

// FalconView(tm) is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// FalconView(tm) is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public License
// along with FalconView(tm).  If not, see <http://www.gnu.org/licenses/>.

// FalconView(tm) is a trademark of Georgia Tech Research Corporation.

// fv_map_scale_util.cpp — portable extraction of CMapScaleUtil. All math is
// preserved verbatim from MapScaleUtil.cpp, including these quirks:
//
//  NOTE (preserved bugs/quirks — fix on both platforms together or not at all):
//   * MAP_SCALE_MILE converts through FEET_TO_METERS: 1 "mile" is treated as
//     0.3048 m. Statute-mile resolutions are wrong by ~5280x, as they are in
//     the shipping Windows build.
//   * The low-precision constants (PI = 3.141592, RAD_TO_DEG = 57.2957,
//     DEG_TO_RAD = 1.7453e-2) differ from geo_tool's higher-precision copies
//     of the same Vincenty code, so distances/bearings here differ slightly
//     from geo_tool's for identical inputs.
//   * ResolutionToScale truncates the result through static_cast<int>.
//   * MAP_SCALE_WORLD yields 2147483647.0 — LONG_MAX on 32-bit-long Windows.
//     Hard-coded here because LONG_MAX on LP64 macOS/Linux is 2^63-1.

#include "fv_map_scale_util.h"

#include <cmath>

namespace fv {

namespace {

const double WORLD_DEG = 360.0;
const double HALF_WORLD_DEG = 180.0;

const double ONE_TO_1M = 1000000.0;
const double ONE_TO_1M_PIXELS_POLE_TO_POLE = 2.0 * 66816.0;

// TIROS 1 km has this many pixels pole to pole
const double ONE_KM_RES_PIXELS_POLE_TO_POLE = 20000.0;

const double kWindowsLongMax = 2147483647.0;  // LONG_MAX with 32-bit long

inline double NM_TO_METERS(double naut_miles) { return naut_miles * 1852; }
inline double KILOMETER_TO_METERS(double km) { return km * 1000.0; }
inline double FEET_TO_METERS(double feet) { return feet * 0.3048; }
inline double YARDS_TO_METERS(double yards) { return yards * 0.9144; }
inline double INCHES_TO_METERS(double inches) { return inches * 0.025399; }
inline double MIN_TO_DEG(double minutes) { return minutes * 1.66667e-2; }
inline double SEC_TO_DEG(double seconds) { return seconds * 2.77778e-4; }
inline double RAD_TO_DEG(double radians) { return radians * 57.2957; }
inline double DEG_TO_RAD(double degrees) { return degrees * 1.7453e-2; }
const double PI = 3.141592;

}  // namespace

HRESULT MapScaleUtil::ResolutionToScale(double dResolution,
                                        MapScaleUnitsEnum eResolutionUnits,
                                        double* pScale) {
  if (dResolution <= 0.0) return E_INVALIDARG;

  if (eResolutionUnits == MAP_SCALE_DENOMINATOR) {
    *pScale = dResolution;
    return S_OK;
  }

  // special case for world scale
  if (eResolutionUnits == MAP_SCALE_WORLD) {
    *pScale = kWindowsLongMax;
    return S_OK;
  }

  double dDegreesLatPerPix;
  HRESULT hr = ResolutionToDegreesLatPerPix(dResolution, eResolutionUnits,
                                            &dDegreesLatPerPix);
  if (FAILED(hr)) return hr;  // invalid units threw in the COM original
  if (dDegreesLatPerPix == 0.0) {
    *pScale = 0;
    return S_OK;
  }

  // if you were to display a world overview equal arc projection display
  // with the resolution at the equator being the input resolution, you
  // would need this many pixels from pole to pole
  double dPixelsPoleToPole = 180.0 / dDegreesLatPerPix;

  // assign a scale that is relative to CADRG
  *pScale = static_cast<int>(ONE_TO_1M * ONE_TO_1M_PIXELS_POLE_TO_POLE /
                             dPixelsPoleToPole);

  return S_OK;
}

HRESULT MapScaleUtil::GetDegreesPerPixel(double latitude, double dScale,
                                         MapScaleUnitsEnum eScaleUnits,
                                         double* degrees_lat_per_pixel,
                                         double* degrees_lon_per_pixel) {
  *degrees_lat_per_pixel = 0.0;
  *degrees_lon_per_pixel = 0.0;

  if (eScaleUnits == MAP_SCALE_WORLD) return E_INVALIDARG;

  double scale = dScale;

  if (eScaleUnits != MAP_SCALE_DENOMINATOR) {
    ResolutionToScale(dScale, eScaleUnits, &scale);
  }

  // compute nominal degrees per pixel lat
  *degrees_lat_per_pixel = GetNominalDegreesLatPerPixel(scale);

  // For scales of 1:10 M and larger you want to maintain the correct ratio
  // between the degrees latitude and longitude per pixel along the center
  // latitude.  That is, you want a small circle to be a circle, when it is
  // centered at center_lat.
  if (scale <= 10000000.0) {
    double d_lat, d_lon;

    ResolutionToDegrees(latitude, 1.0, d_lat, d_lon);

    *degrees_lon_per_pixel = *degrees_lat_per_pixel * d_lon / d_lat;
  } else {
    *degrees_lon_per_pixel = *degrees_lat_per_pixel;
  }

  return S_OK;
}

HRESULT MapScaleUtil::GetDegreesPerPixelWorld(long surface_width,
                                              long surface_height,
                                              double* degrees_lat_per_pixel,
                                              double* degrees_lon_per_pixel) {
  if (surface_width <= 0 || surface_height <= 0) return E_INVALIDARG;

  // measure from center of pixels
  *degrees_lat_per_pixel = HALF_WORLD_DEG / (double)(surface_height);
  *degrees_lon_per_pixel = WORLD_DEG / (double)(surface_width);

  return S_OK;
}

// Returns E_INVALIDARG for unknown units (thrown in the COM original);
// a Vincenty failure yields S_OK with *pDegreesLatPerPix = 0.0, matching the
// original's "return 0.0" path.
HRESULT MapScaleUtil::ResolutionToDegreesLatPerPix(
    double dResolution, MapScaleUnitsEnum eResolutionUnits,
    double* pDegreesLatPerPix) {
  // the number of degrees of latitude covered by 1 pixel centered on the
  // equator will determine m_scale by comparing the dpp value of this map
  // scale to the nominal dpp values of a 1:1 M chart
  double dDegreesLatPerPixel = 0.0;
  double dDegreesLonPerPixel;

  *pDegreesLatPerPix = 0.0;

  switch (eResolutionUnits) {
    case MAP_SCALE_ARC_DEGREES:
      *pDegreesLatPerPix = dResolution;
      return S_OK;

    case MAP_SCALE_ARC_MINUTES:
      *pDegreesLatPerPix = MIN_TO_DEG(dResolution);
      return S_OK;

    case MAP_SCALE_ARC_SECONDS:
      *pDegreesLatPerPix = SEC_TO_DEG(dResolution);
      return S_OK;

    default:
      break;
  }

  double dResolutionMeters;
  HRESULT hr = ConvertResolutionToMeters(dResolution, eResolutionUnits,
                                         &dResolutionMeters);
  if (FAILED(hr)) return hr;
  if (!ResolutionToDegrees(0.0, dResolutionMeters, dDegreesLatPerPixel,
                           dDegreesLonPerPixel))
    return S_OK;  // *pDegreesLatPerPix stays 0.0

  *pDegreesLatPerPix = dDegreesLatPerPixel;
  return S_OK;
}

bool MapScaleUtil::ResolutionToDegrees(double dCenterLat,
                                       double dResolutionMeters,
                                       double& dDegreesLatPerPix,
                                       double& dDegreesLonPerPix) {
  double dLat1, dLon1, dLat2, dLon2;
  double dDistanceLat, dDistanceLon;
  double dBearing;

  // 0.01 degrees of latitude is equal to 0.6 minutes of latitude, which is
  // roughly equivalent to 0.6 NM which equals 1111.2 meters.  Similarly 0.01
  // degrees of longitude at the equator is roughtly eqivalent to 1111.2
  // meters.  0.01 degrees of latitude will cover 1111.2 meters +/- 0.1% any
  // where.  As you move away from the equator 0.01 degrees of longitude
  // covers significantly fewer meters.

  // To avoid doing great circle calculations with points that are extremely
  // close together a geo-box of fixed size was choosen rather than trying to
  // use a resolution_meters square.

  // calculate the distance in meters of 0.01 degree of latitude at lat
  if (dCenterLat < 89.0) {
    dLat1 = dCenterLat + 0.005;
    dLat2 = dCenterLat - 0.005;
  } else {
    dLat1 = dCenterLat - 0.005;
    dLat2 = dCenterLat - 2 * 0.005;
  }

  dLon1 = dLon2 = 0.0;
  if (!CalcRangeAndBearing(dLat1, dLon1, dLat2, dLon2, &dDistanceLat,
                           &dBearing))
    return false;

  // calculate the distance in meters of 0.01 degree of longitude at lat
  dLon1 = 0.005;
  dLon2 = -0.005;
  dLat1 = dLat2 = dCenterLat;
  if (!CalcRangeAndBearing(dLat1, dLon1, dLat2, dLon2, &dDistanceLon,
                           &dBearing))
    return false;

  // degrees / meters * meters per pixel = degrees per pixel
  dDegreesLatPerPix = 0.01 / dDistanceLat * dResolutionMeters;
  dDegreesLonPerPix = 0.01 / dDistanceLon * dResolutionMeters;

  return true;
}

// The algorithm was published by T.Vincenty in Survey Review, NO 176, 1975.
// (This is the module's own lower-precision copy; see NOTE at top of file.)
bool MapScaleUtil::CalcRangeAndBearing(double pt1Lat, double pt1Lon,
                                       double pt2Lat, double pt2Lon,
                                       double* mag, double* dir) {
  const long double A = 6378137.0;       // semi-major axis of ellipsoid
  const long double RECF = 298.257223563;  // reciprocal of flattening (1/F)

  const long double EPS = 1.0E-11;

  long double C2SIGM, CDLAMS, COSSAZ, COSSIG, COSU1, COSU2, DENOM, DLAM, DLAMS,
      RNUMER, SDLAMS, SIG, SINAZ, SINSIG, SINU1, SINU2, TA, TB, TC, TEMP, US;
  int iIteration, cnt;

  // set default values for return params
  *mag = 0.0;
  *dir = 0.0;

  // convert to radians
  pt1Lat = DEG_TO_RAD(pt1Lat);
  pt1Lon = DEG_TO_RAD(pt1Lon);
  pt2Lat = DEG_TO_RAD(pt2Lat);
  pt2Lon = DEG_TO_RAD(pt2Lon);

  // adjust values
  if (pt1Lon > PI) pt1Lon = pt1Lon - 2.0 * PI;
  if (pt1Lon < -PI) pt1Lon = pt1Lon + 2.0 * PI;
  if (pt2Lon > PI) pt2Lon = pt2Lon - 2.0 * PI;
  if (pt2Lon < -PI) pt2Lon = pt2Lon + 2.0 * PI;

  // If the points are colocated
  if (fabs(pt1Lat - pt2Lat) < EPS && fabs(pt1Lon - pt2Lon) < EPS) {
    *mag = 0.00;
    *dir = 0.00;
    return true;
  }

  // SEMI MAJOR AXIS - B
  const double B = A * (RECF - 1.0) / RECF;

  /* FLATTENING (F) */
  const double FL = 1.0 / RECF;

  iIteration = 0;

  /* TANGENT OF REDUCED LATITUDE (U) OF POINT 1 & 2 */
  const double TANU1 = (1.0 - FL) * sin(pt1Lat) / cos(pt1Lat);
  const double TANU2 = (1.0 - FL) * sin(pt2Lat) / cos(pt2Lat);

  /* COSINE & SINE OF U1 & U2 FROM TRIG IDENTITIES */
  COSU1 = 1.0 / sqrt(1.0 + TANU1 * TANU1);
  SINU1 = TANU1 * COSU1;
  COSU2 = 1.0 / sqrt(1.0 + TANU2 * TANU2);
  SINU2 = TANU2 * COSU2;

  if (COSU1 == 1.0) SINU1 = 0.00;

  if (COSU2 == 1.0) SINU2 = 0.00;

  /* DIFFERENCE IN LONGITUDE */
  DLAM = pt2Lon - pt1Lon;

  /* 1ST ESTIMATE DLAMS - DIFF IN LONGITUDE ON AUXILIARY SPHERE */
  DLAMS = DLAM;

  do {
    /* SINE & COSINE OF DLAMS */
    SDLAMS = sin(DLAMS);
    CDLAMS = cos(DLAMS);

    /* SINE & COSINE OF SIGMA */
    SINSIG = sqrt(COSU2 * SDLAMS * COSU2 * SDLAMS +
                  (COSU1 * SINU2 - SINU1 * COSU2 * CDLAMS) *
                      (COSU1 * SINU2 - SINU1 * COSU2 * CDLAMS));

    COSSIG = SINU1 * SINU2 + COSU1 * COSU2 * CDLAMS;

    /* SIGMA */
    SIG = atan2(SINSIG, COSSIG);

    /* SINE OF AZIMUTH (AZ) OF GEODESIC AT EQUATOR */
    SINAZ = COSU1 * COSU2 * SDLAMS / SINSIG;

    /* COSINE SQUARED OF AZ USING TRIG IDENTITY */
    COSSAZ = 1.0 - SINAZ * SINAZ;

    /* COSINE OF 2 SIGMA-SUB-M */
    if (SINU1 == 0.0 || SINU2 == 0.0)
      C2SIGM = COSSIG;
    else
      C2SIGM = COSSIG - 2.0 * SINU1 * SINU2 / COSSAZ;

    /* TERM C */
    TC = FL * COSSAZ * (4.0 + FL * (4.0 - 3.0 * COSSAZ)) / 16.0;

    /* SAVE PREVIOUS DIFF IN LONGITUDE ON AUXILIARY SPHERE */
    TEMP = DLAMS;

    /* NEWEST DIFFERENCE IN LONGITUDE ON AUXILIARY SPHERE */
    DLAMS = DLAM + (1.0 - TC) * FL * SINAZ *
                       (SIG + TC * SINSIG *
                                  (C2SIGM + TC * COSSIG *
                                                (-1.0 + 2.0 * C2SIGM * C2SIGM)));

    iIteration++;

    /* TEST FOR NEARLY ANTIPODAL LINE CONDITION */
    if (fabsl(DLAMS) > PI && iIteration > 50) { /* ANTIPODAL CONDITION */
      *mag = 0.0;
      *dir = 0.0;
      return false;
    }

  } while (fabsl(TEMP - DLAMS) > EPS);

  /* SMALL u SQUARED (US) */
  US = COSSAZ * A * A / (B * B) - 1;

  /* FORWARD AZIMUTH FROM NORTH */
  /* NUMERATOR & DENOMINATOR */
  RNUMER = COSU2 * SDLAMS;
  DENOM = COSU1 * SINU2 - SINU1 * COSU2 * CDLAMS;
  *dir = atan2((double)RNUMER, (double)DENOM);
  if (*dir < 0.0) *dir = *dir + 2.0 * PI;

  /* TERM A */
  TA = 1.0 + US * (4096.0 + US * (-768.0 + US * (320.0 - 175.0 * US))) /
                 16384.0;

  /* TERM B */
  TB = US * (256.0 + US * (-128.0 + US * (74.0 - 47.0 * US))) / 1024.0;

  /* GEODETIC DISTANCE */
  *mag = B * TA *
         (SIG - TB * SINSIG *
                    (C2SIGM + TB * (COSSIG * (-1.0 + 2.0 * C2SIGM * C2SIGM) -
                                    TB * C2SIGM *
                                        (-3.0 + 4.0 * SINSIG * SINSIG) *
                                        (-3.0 + 4.0 * C2SIGM * C2SIGM) / 6.0) /
                               4.0));

  cnt = 0;
  while (*dir < 0.0) {
    *dir += (2 * PI);
    cnt++;
    // too many iterations here indicate a bad value of *dir
    if (cnt > 10) return false;
  }
  cnt = 0;
  while (*dir > (2 * PI)) {
    *dir -= (2 * PI);
    cnt++;
    // too many iterations here indicate a bad value of *dir
    if (cnt > 10) return false;
  }

  if (*mag < 1e-9) *dir = 0.0;

  *dir = RAD_TO_DEG(*dir);

  return true;
}

HRESULT MapScaleUtil::ConvertResolutionToMeters(
    double dResolution, MapScaleUnitsEnum eResolutionUnits, double* pMeters) {
  switch (eResolutionUnits) {
    case MAP_SCALE_NM:
      *pMeters = NM_TO_METERS(dResolution);
      return S_OK;

    case MAP_SCALE_MILE:
      // NOTE: preserved bug — statute miles converted as feet (see file NOTE)
      *pMeters = FEET_TO_METERS(dResolution);
      return S_OK;

    case MAP_SCALE_KILOMETER:
      *pMeters = KILOMETER_TO_METERS(dResolution);
      return S_OK;

    case MAP_SCALE_METERS:
      *pMeters = dResolution;
      return S_OK;

    case MAP_SCALE_YARDS:
      *pMeters = YARDS_TO_METERS(dResolution);
      return S_OK;

    case MAP_SCALE_FEET:
      *pMeters = FEET_TO_METERS(dResolution);
      return S_OK;

    case MAP_SCALE_INCHES:
      *pMeters = INCHES_TO_METERS(dResolution);
      return S_OK;

    // NOTE: preserved quirk — the original returns degrees, not meters, for
    // the ARC units from this "ToMeters" function (unreachable through the
    // public API, which filters ARC units earlier).
    case MAP_SCALE_ARC_DEGREES:
      *pMeters = dResolution;
      return S_OK;

    case MAP_SCALE_ARC_MINUTES:
      *pMeters = MIN_TO_DEG(dResolution);
      return S_OK;

    case MAP_SCALE_ARC_SECONDS:
      *pMeters = SEC_TO_DEG(dResolution);
      return S_OK;

    default:
      return E_INVALIDARG;  // the COM original threw E_INVALIDARG
  }
}

// Returns a nominal degrees latitude per pixel value for displaying a maps
// at this MapScale.  This value, or one close to it, should be used for
// displaying all map data on a monitor.  If you want to know the relative
// difference between two different MapScale objects you can use the ratio
// of their degrees latitude per pixel values.
double MapScaleUtil::GetNominalDegreesLatPerPixel(double dScale) {
  // base map scales of 1:5 M or larger off of 1:1 M CADRG
  if (dScale <= 5000000) {
    return (((double)dScale / 1000000.0) *
            (180.0 / ONE_TO_1M_PIXELS_POLE_TO_POLE));
  }
  // base small scale maps off of 1 km TIROS
  else {
    double scale = 1.0;
    ResolutionToScale(1.0, MAP_SCALE_KILOMETER, &scale);

    return (((double)dScale / scale) * (180.0 / ONE_KM_RES_PIXELS_POLE_TO_POLE));
  }
}

}  // namespace fv

// Copyright (c) 1994-2011 Georgia Tech Research Corporation, Atlanta, GA
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

// Copyright (c) 1994-2011 Georgia Tech Research Corporation, Atlanta, GA
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

// fv_geoid.cpp — portable extraction of CGeoid (fvw_core/geoid/Geoid1.cpp).
// The numeric behavior (grid layout, edge clamping, bilinear interpolation,
// meters->feet conversion) is preserved exactly.

#include "fv_geoid.h"

#include <cmath>
#include <cstdio>

namespace fv {

namespace {

// Grid dimensions (values preserved from Geoid1.h).
// EGM96/EGM08 grids: 15-minute spacing; EGM84: 30-minute spacing.
const size_t kGeoidCols = 1441;
const size_t kGeoidRows = 721;
const size_t kGeoid84Cols = 721;
const size_t kGeoid84Rows = 361;

const int kDatumEGM84 = 1;
const int kDatumEGM96 = 2;
const int kDatumEGM08 = 3;

const int kSuccess = 0;
const int kFailure = -1;

const double kMetersToFeet = 3.28083989501;

}  // namespace

GeoidCalculator::GeoidCalculator(std::string data_directory)
    : m_data_directory(std::move(data_directory)), m_datum_code(kDatumEGM96) {
  if (!m_data_directory.empty()) {
    char last = m_data_directory[m_data_directory.size() - 1];
    if (last != '/' && last != '\\') m_data_directory += '/';
  }
}

int GeoidCalculator::set_vertical_datum(int datum_year, int* err_code,
                                        std::string* err_msg) {
  *err_code = 0;
  switch (datum_year) {
    case 1984:
      m_datum_code = kDatumEGM84;
      break;
    case 1996:
      m_datum_code = kDatumEGM96;
      break;
    case 2008:
      m_datum_code = kDatumEGM08;
      break;
    default:
      *err_code = GEOID_DATUM_CODE_ERROR;
      *err_msg = "Invalid Vertical Datum Code";
      return kFailure;
  }
  return kSuccess;
}

int GeoidCalculator::get_geoid_delta(double latitude, double longitude,
                                     double* delta, int* err_code,
                                     std::string* err_msg) {
  int rslt = init_grid(err_code, err_msg);
  if (rslt != kSuccess) return rslt;
  return interpolate(latitude, longitude, delta, err_code, err_msg);
}

int GeoidCalculator::init_grid(int* err_code, std::string* err_msg) {
  switch (m_datum_code) {
    case kDatumEGM84:
      return load_grid("egm84.dat", 0.5f, kGeoid84Rows, kGeoid84Cols, &m_egm84,
                       err_code, err_msg);
    case kDatumEGM96:
      return load_grid("egm96.dat", 0.25f, kGeoidRows, kGeoidCols, &m_egm96,
                       err_code, err_msg);
    case kDatumEGM08:
      return load_grid("egm08.dat", 0.25f, kGeoidRows, kGeoidCols, &m_egm08,
                       err_code, err_msg);
    default:
      *err_code = GEOID_DATUM_CODE_ERROR;
      *err_msg = "Invalid Geoid Datum Code";
      return kFailure;
  }
}

// File format: six 4-byte little-endian floats (min lat, max lat, min lon,
// max lon, lat spacing, lon spacing) followed by rows*cols 4-byte floats,
// row-major from the northwest corner (90N, 0E).
int GeoidCalculator::load_grid(const char* file_name, float expected_spacing,
                               size_t rows, size_t cols, Grid* grid,
                               int* err_code, std::string* err_msg) {
  if (grid->inited) return kSuccess;

  std::string filename = m_data_directory + file_name;
  FILE* fp = std::fopen(filename.c_str(), "rb");
  if (fp == nullptr) {
    *err_code = GEOID_FILE_OPEN_ERROR;
    *err_msg = "Cannot open file: " + filename;
    return kFailure;
  }

  float header[6];  // minlat, maxlat, minlon, maxlon, latspace, lonspace
  if (std::fread(header, sizeof(float), 6, fp) != 6 || header[0] != -90.0f ||
      header[1] != 90.0f || header[2] != 0.0f || header[3] != 360.0f ||
      header[4] != expected_spacing || header[5] != expected_spacing) {
    *err_code = GEOID_INITIALIZE_ERROR;
    *err_msg = "Improper header in file: " + filename;
    std::fclose(fp);
    return kFailure;
  }

  const size_t num_elevs = rows * cols;
  grid->heights.resize(num_elevs);
  size_t num = std::fread(grid->heights.data(), sizeof(float), num_elevs, fp);
  std::fclose(fp);

  if (num != num_elevs) {
    grid->heights.clear();
    *err_code = GEOID_INITIALIZE_ERROR;
    *err_msg = "Incomplete data in file: " + filename;
    return kFailure;
  }

  grid->inited = true;
  return kSuccess;
}

int GeoidCalculator::interpolate(double latitude, double longitude,
                                 double* delta, int* err_code,
                                 std::string* err_msg) {
  *err_code = 0;

  double scalefactor = 4.0;  // grid cells per degree
  double num_col = static_cast<double>(kGeoidCols);
  double num_row = static_cast<double>(kGeoidRows);
  const Grid* grid;

  switch (m_datum_code) {
    case kDatumEGM84:
      grid = &m_egm84;
      scalefactor = 2.0;
      num_col = static_cast<double>(kGeoid84Cols);
      num_row = static_cast<double>(kGeoid84Rows);
      break;
    case kDatumEGM96:
      grid = &m_egm96;
      break;
    case kDatumEGM08:
      grid = &m_egm08;
      break;
    default:
      *err_code = GEOID_DATUM_CODE_ERROR;
      *err_msg = "Invalid Geoid Datum Code";
      return kFailure;
  }

  if (!grid->inited) {
    *err_code = GEOID_NOT_INITIALIZED_ERROR;
    *err_msg = "Geoid not initialized";
    return GEOID_NOT_INITIALIZED_ERROR;
  }

  err_msg->clear();
  if (latitude < -90.0 || latitude > 90.0) {
    *err_code |= GEOID_LAT_ERROR;
    *err_msg += "Latitude out of range.";
  }
  if (longitude < -180.0 || longitude > 180.0) {
    *err_code |= GEOID_LON_ERROR;
    *err_msg += " Longitude out of range.";
  }
  if (*err_code != 0) return *err_code;

  // Compute X and Y offsets into the geoid height array. (0,0) is at the
  // northwest corner (90N, 0E); longitude wraps west to [180, 360).
  double offset_x =
      (longitude < 0.0 ? longitude + 360.0 : longitude) * scalefactor;
  double offset_y = (90.0 - latitude) * scalefactor;

  // Four nearest posts, clamped so post+1 stays inside the grid.
  double post_x = std::floor(offset_x);
  if (post_x + 1 == num_col) post_x--;
  double post_y = std::floor(offset_y);
  if (post_y + 1 == num_row) post_y--;

  const std::vector<float>& h = grid->heights;
  long index = static_cast<long>(post_y * num_col + post_x);
  double elevation_nw = h[index];
  double elevation_ne = h[index + 1];
  index = static_cast<long>((post_y + 1) * num_col + post_x);
  double elevation_sw = h[index];
  double elevation_se = h[index + 1];

  // Bilinear interpolation. NOTE: preserved verbatim from Geoid1.cpp,
  // including its in-cell latitude inversion: delta_y measures the fraction
  // SOUTH of the northern posts, yet the final blend runs from the southern
  // row toward the northern one, so the result is effectively evaluated at
  // (1 - delta_y). Error is bounded by the geoid change across one grid cell
  // (sub-meter). Kept so results match the Windows build exactly; fix on both
  // platforms together or golden-file comparisons will diverge.
  double delta_x = offset_x - post_x;
  double delta_y = offset_y - post_y;
  double upper_y = elevation_nw + delta_x * (elevation_ne - elevation_nw);
  double lower_y = elevation_sw + delta_x * (elevation_se - elevation_sw);
  *delta = lower_y + delta_y * (upper_y - lower_y);

  // Convert from meters to feet (behavior preserved from the COM interface).
  *delta *= kMetersToFeet;

  return *err_code;
}

}  // namespace fv

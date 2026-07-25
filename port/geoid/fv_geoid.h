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

// fv_geoid.h — portable extraction of the geoid-separation logic from
// fvw_core/geoid/Geoid1.cpp (CGeoid). The ATL COM wrapper and the registry
// lookup of the data directory stay Windows-only; here the data directory is
// supplied by the caller.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fv {

// Error codes (values preserved from Geoid1.h)
enum GeoidError {
  GEOID_NO_ERROR = 0x0000,
  GEOID_FILE_OPEN_ERROR = 0x0001,
  GEOID_INITIALIZE_ERROR = 0x0002,
  GEOID_NOT_INITIALIZED_ERROR = 0x0004,
  GEOID_LAT_ERROR = 0x0008,
  GEOID_LON_ERROR = 0x0010,
  GEOID_FILE_SEEK_ERROR = 0x0020,
  GEOID_FILE_READ_ERROR = 0x0040,
  GEOID_DATUM_CODE_ERROR = 0x0080,
};

class GeoidCalculator {
 public:
  // data_directory: directory containing egm84.dat / egm96.dat / egm08.dat
  // (the Windows product resolves this via the registry; see get_default_source
  // in Geoid1.cpp). Trailing separator optional.
  explicit GeoidCalculator(std::string data_directory);

  // datum_year: 1984, 1996 (default) or 2008.
  // Returns 0 on success; on failure returns nonzero and sets err_code/err_msg.
  int set_vertical_datum(int datum_year, int* err_code, std::string* err_msg);

  // Height of the WGS84 geoid above/below the WGS84 ellipsoid at the given
  // geodetic coordinates (decimal degrees), bilinearly interpolated from the
  // selected EGM grid. *delta is returned in FEET (matches the original COM
  // interface's get_geoid_delta). Lazily loads the grid file on first use.
  // Returns 0 on success; nonzero error code otherwise.
  int get_geoid_delta(double latitude, double longitude, double* delta,
                      int* err_code, std::string* err_msg);

 private:
  struct Grid {
    std::vector<float> heights;
    bool inited = false;
  };

  int init_grid(int* err_code, std::string* err_msg);
  int load_grid(const char* file_name, float expected_spacing, size_t rows,
                size_t cols, Grid* grid, int* err_code, std::string* err_msg);
  int interpolate(double latitude, double longitude, double* delta,
                  int* err_code, std::string* err_msg);

  std::string m_data_directory;
  int m_datum_code;  // internal code, see fv_geoid.cpp
  Grid m_egm84;
  Grid m_egm96;
  Grid m_egm08;
};

}  // namespace fv

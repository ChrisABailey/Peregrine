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

// fv_dted_cell.h — portable extraction of CDtedMemoryMappedFile
// (fvw_core/MapDataServer/DtedMapServer/DtedElevation.cpp): opens one DTED
// cell file (.dt0/.dt1/.dt2/.dt3) and answers single-post elevation queries.
// The COM service above it (CDted: caching, block fills, level fallback,
// notify events) stays Windows-only; a portable cache can be layered here
// later if profiling demands it.

#pragma once

#include <string>

#include "fv_compat.h"
#include "fv_filemap.h"

namespace fv {

// Values from MdsInterfaces/DtedMapServer.idl (COM ABI — never renumber)
enum DtedElevationUnitsEnum {
  DTED_ELEVATION_FEET = 0,
  DTED_ELEVATION_METERS = 1
};
enum DtedElevationValueEnum {
  PARTIAL_DTED_ELEVATION = -32767,
  MISSING_DTED_ELEVATION = -99999
};

class DtedCell {
 public:
  // Opens and validates a DTED cell file. As in the original, the cell's
  // geographic bounds do NOT come from the file header: FalconView assigns
  // them from the tile path (dted/w106/n40.dt3 -> SW corner 40N 106W), so
  // the caller passes the SW corner here.
  bool Open(const std::string& filename, double sw_lat, double sw_lon);
  void Close();
  bool IsOpen() const { return m_map.IsOpen(); }

  // Elevation of the post nearest to (lat, lon). Returns false if the
  // coordinate is outside this cell or the file read fails. Elevation may be
  // PARTIAL_DTED_ELEVATION (-32767) for void posts in partial cells, as on
  // Windows.
  bool GetElevation(double lat, double lon, DtedElevationUnitsEnum eUnits,
                    long& lElevation) const;

  int EwPosts() const { return m_ew_posts; }
  int NsPosts() const { return m_ns_posts; }
  double SwLat() const { return m_ll_lat; }
  double SwLon() const { return m_ll_lon; }

 private:
  bool CalculateDTEDParameters();

  MappedFile m_map;
  std::string m_fileName;
  double m_ll_lat = 0, m_ll_lon = 0, m_ur_lat = 0, m_ur_lon = 0;
  int m_ew_posts = 0;
  int m_ns_posts = 0;
  unsigned m_data_record_length = 0;
};

}  // namespace fv

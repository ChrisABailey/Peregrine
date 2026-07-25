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

// fv_dted_cell.cpp — portable extraction of CDtedMemoryMappedFile. Logic is
// preserved from DtedElevation.cpp with these platform substitutions only:
// CreateFileMapping windowing -> whole-file fv::MappedFile (byte-identical
// reads); SEH __try around the view read -> not needed (no remote-file
// EXCEPTION_IN_PAGE_ERROR model on POSIX mmap of local files).
//
// NOTE (preserved behavior):
//  * Post selection is nearest-neighbor ((offset * (posts-1)) + 0.5 cast to
//    int) — DTED lookups do not interpolate.
//  * Elevations are big-endian SIGNED-MAGNITUDE shorts: negative values are
//    decoded as -(e & 0x7FFF).
//  * Bounds check allows 1e-7 slack above the upper-right corner; negative
//    offsets clamp to 0 (round-off tolerance near the SW edges).
//  * PARTIAL_DTED_ELEVATION (-32767) passes through unconverted even when
//    feet are requested.

#include "fv_dted_cell.h"

#include <cmath>

namespace fv {

namespace {

// File layout constants (DtedElevation.h / DTEDFileFormat.h; MIL-PRF-89020)
const unsigned kUhlLength = 80;
const unsigned kDsiLength = 648;
const unsigned kAccLength = 2700;
const unsigned kDrdLength = 8;  // data record header: sentinel+count+lon+lat
const unsigned kDataRecordsOffset = kUhlLength + kDsiLength + kAccLength;
const unsigned kDataRecordsDataOffset = kDataRecordsOffset + kDrdLength;
const unsigned kDatumSize = 2;      // sizeof(DTED_DATA_REC_DATUM)
const unsigned kTrailerSize = 4;    // sizeof(DTED_DATA_REC_TRLR)

// UHL field offsets used here (struct DTED_UHL)
const unsigned kUhlNumLonLines = 3 + 1 + 8 + 8 + 4 + 4 + 4 + 3 + 12;  // 47
const unsigned kUhlNumLatPoints = kUhlNumLonLines + 4;                // 51

const double kNimaDtedFrameGeoHeight = 1.0;  // degrees
const double kNimaDtedFrameGeoWidth = 1.0;

inline double METERS_TO_FEET(double meters) { return meters * 3.28083989501; }

// sscanf_s("%4d") equivalent for a fixed-width ASCII field
bool ParseAsciiInt4(const unsigned char* p, int* out) {
  char buf[5] = {(char)p[0], (char)p[1], (char)p[2], (char)p[3], 0};
  return sscanf_s(buf, "%4d", out) == 1;
}

}  // namespace

bool DtedCell::Open(const std::string& filename, double sw_lat,
                    double sw_lon) {
  Close();
  m_fileName = filename;
  if (!m_map.Open(filename)) return false;

  if (m_map.Size() < kDataRecordsDataOffset) {
    Close();
    return false;
  }

  if (!CalculateDTEDParameters()) {
    Close();
    return false;
  }

  // Cell bounds assigned from the tile grid, exactly as the Windows cache
  // does (floor of the requested coordinate = the path-derived SW corner).
  m_ll_lat = std::floor(sw_lat);
  m_ll_lon = std::floor(sw_lon);
  m_ur_lat = m_ll_lat + kNimaDtedFrameGeoHeight;
  m_ur_lon = m_ll_lon + kNimaDtedFrameGeoWidth;

  return true;
}

void DtedCell::Close() {
  m_map.Close();
  m_ll_lat = m_ll_lon = m_ur_lat = m_ur_lon = 0.0;
  m_ew_posts = m_ns_posts = 0;
  m_data_record_length = 0;
}

// NOTE: DTED level 3 is out of scope (per Chris, 2026-07-11: NGA produced
// only a handful of prototype .DT3 cells ever; only levels 1 and 2 matter).
// The sample n40.DT3 in TestData uses a nonstandard prototype UHL that even
// this snapshot's Windows reader would reject — files failing the standard
// header parse below are simply rejected.
bool DtedCell::CalculateDTEDParameters() {
  const unsigned char* uhl = m_map.Data();

  if (!ParseAsciiInt4(uhl + kUhlNumLonLines, &m_ew_posts) ||
      m_ew_posts < 21 || m_ew_posts > 9001)
    return false;

  if (!ParseAsciiInt4(uhl + kUhlNumLatPoints, &m_ns_posts) ||
      m_ns_posts < 21 || m_ns_posts > 9001)
    return false;

  // Length of one full N-S column of data
  m_data_record_length =
      kDrdLength + (m_ns_posts * kDatumSize) + kTrailerSize;

  return true;
}

bool DtedCell::GetElevation(double lat, double lon,
                            DtedElevationUnitsEnum eUnits,
                            long& lElevation) const {
  if (!IsOpen()) return false;

  if (lat > m_ur_lat + 1e-7 || lon > m_ur_lon + 1e-7) return false;

  double offset_from_lower_lat = lat - m_ll_lat;
  double offset_from_left_lon = lon - m_ll_lon;

  // Allow for round off error (see original comment block): clamp small
  // negative offsets to zero.
  if (offset_from_left_lon < 0.0) offset_from_left_lon = 0.0;
  if (offset_from_lower_lat < 0.0) offset_from_lower_lat = 0.0;

  // Tiles are assumed to be 1x1 degrees; nearest post
  int lon_index =
      static_cast<int>((offset_from_left_lon * (m_ew_posts - 1)) + 0.5);
  int lat_index =
      static_cast<int>((offset_from_lower_lat * (m_ns_posts - 1)) + 0.5);

  unsigned long fileLoc = kDataRecordsDataOffset +
                          (m_data_record_length * (unsigned long)lon_index) +
                          (kDatumSize * (unsigned long)lat_index);

  if (fileLoc + kDatumSize > m_map.Size()) return false;

  const unsigned char* pbDatum = m_map.Data() + fileLoc;

  // Byte swap (Motorola -> Intel)
  short elevation;
  ((unsigned char*)&elevation)[0] = pbDatum[1];
  ((unsigned char*)&elevation)[1] = pbDatum[0];

  // The DTED file stores numbers as signed magnitude (not complemented
  // bits). If the high bit is set (negative here), put the number in our
  // format, 2's complement.
  if (elevation < 0) elevation = -(elevation & 0x7FFF);

  lElevation = static_cast<long>(elevation);

  if (eUnits == DTED_ELEVATION_FEET && elevation != PARTIAL_DTED_ELEVATION)
    lElevation = static_cast<long>(METERS_TO_FEET(lElevation));

  return true;
}

}  // namespace fv

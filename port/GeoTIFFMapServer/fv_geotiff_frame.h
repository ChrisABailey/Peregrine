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

// fv_geotiff_frame.h — portable twin of CGeoTiffFrameFile's COM surface
// (raw_GetFrameProperties): identifies a GeoTIFF frame (USGS DRG / DOQ /
// generic geodata), returns WGS-84 coverage bounds, scale, and series.

#pragma once

#include <string>

#include "fv_compat.h"
#include "fv_map_enums.h"

class CGeoTiffFrameFile;  // shared parser, compiled in place

namespace fv {

struct GeoTiffFrameProperties {
  double ll_lat = 0, ll_lon = 0, ur_lat = 0, ur_lon = 0;  // WGS-84
  double scale_denom = 0;
  short scale_units = MAP_SCALE_DENOMINATOR;  // MapScaleUnitsEnum
  std::wstring series;
  bool supported = false;
};

class GeoTiffFrame {
 public:
  GeoTiffFrame();
  ~GeoTiffFrame();

  // Mirrors put_m_FilePath / put_m_FileName + raw_GetFrameProperties.
  // file_path must end with a separator, as on Windows (path + name are
  // concatenated directly). NOTE: the Windows NOT_SUPPORTED path falls back
  // to the ImageLib COM object (MrSID, misc formats); on POSIX that fallback
  // does not exist yet, so such frames report supported = false.
  HRESULT GetFrameProperties(const std::string& file_path,
                             const std::string& file_name,
                             GeoTiffFrameProperties* props);

  // Access to the underlying parser for pixel/geo transforms.
  CGeoTiffFrameFile* Parser() { return m_file; }

 private:
  CGeoTiffFrameFile* m_file;
};

}  // namespace fv

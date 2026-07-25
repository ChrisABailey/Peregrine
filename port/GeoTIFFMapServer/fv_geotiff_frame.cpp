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

// fv_geotiff_frame.cpp — faithful extraction of
// CGeoTiffFrameFile::raw_GetFrameProperties (GeoTiffFrameFile.cpp), with:
//   IDatumConvertPtr/CO_CREATE  -> fv::IDatumConvert (GEOTRANS-backed)
//   BSTR/VARIANT_BOOL outputs   -> std::wstring / bool
//   ImageLib COM fallback       -> reports unsupported (NOTE in header)
// Scale-table logic, DRG/DOQ checks, goto flow and datum-code mapping are
// preserved verbatim.

#include "fv_geotiff_frame.h"

#include <cctype>

#include "GeoTiffFrameFile.h"
#include "GeoTiff_defines.h"
#include "fv_interfaces.h"

namespace fv {

GeoTiffFrame::GeoTiffFrame() : m_file(new CGeoTiffFrameFile) {}
GeoTiffFrame::~GeoTiffFrame() { delete m_file; }

HRESULT GeoTiffFrame::GetFrameProperties(const std::string& file_path,
                                         const std::string& file_name,
                                         GeoTiffFrameProperties* props) {
  BOOL image_type_supported, geodata_present, geodata_supported;
  int status, error_code, image_type, image_width, image_length;
  std::string image_type_description, error_message;
  CGeoTiffFrameFile& gtf = *m_file;
  CGeoData geodata;
  std::string image_description;
  std::string datum_string;
  std::wstring series_str;
  int scale_from_file_name;

  props->supported = false;
  props->series.clear();

  IDatumConvert* datum_convert_ptr = EnsureDefaultDatumConverter();
  if (datum_convert_ptr == nullptr) return E_FAIL;

  double geotiff_ll_lat, geotiff_ll_lon, geotiff_ur_lat, geotiff_ur_lon;
  double *ll_lat = &props->ll_lat, *ll_lon = &props->ll_lon;
  double *ur_lat = &props->ur_lat, *ur_lon = &props->ur_lon;
  double* scale_denom = &props->scale_denom;
  short* sScaleUnits = &props->scale_units;

  // try to load the file with the geotiff class library
  std::string tiff_file_path(file_path + file_name);
  status = gtf.load(tiff_file_path, image_type_supported, image_type,
                    image_type_description, image_width, image_length,
                    geodata_present, geodata_supported, error_message);

  // return FAILURE if the load failed, the image type is not supported,
  // or if the georeferencing data is not supported
  if (status != SUCCESS || image_width < 3 || image_length < 3 ||
      !image_type_supported || !geodata_supported)
    goto NOT_SUPPORTED;

  // get the geodata structure
  if (gtf.get_geodata(geodata, error_code, error_message) != SUCCESS)
    goto NOT_SUPPORTED;
  // determine datum string
  switch (geodata.m_geog_geodetic_datum) {
    case GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1927:
      datum_string = "NAS-C";
      break;
    case GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1983:
      datum_string = "NAR-C";
      break;
    case GEOTIFF_DATUM_WGS72:
      datum_string = "W72";
      break;
    case GEOTIFF_DATUM_WGS84:
      datum_string = "W84";
      break;
    default:
      goto NOT_SUPPORTED;
  }

  // get the image description
  if (gtf.get_image_description(image_description) != SUCCESS)
    image_description = "";
  // (original used _tcsupr_s on the string's internal buffer)
  for (size_t i = 0; i < image_description.size(); ++i)
    image_description[i] = (char)toupper((unsigned char)image_description[i]);

  // check for USGS DRG *************************************************
  if (image_description.find("USGS GEOTIFF DRG") == std::string::npos)
    goto CHECK_DOQ;

  // determine scale and coverage from file name
  if (gtf.TIF_get_drg_coverage(file_name, scale_from_file_name,
                               geotiff_ll_lat, geotiff_ll_lon, geotiff_ur_lat,
                               geotiff_ur_lon) != SUCCESS)
    goto CHECK_DOQ;

  // convert datum for coverage lat/lon's to WGS84
  datum_convert_ptr->ConvertDatum(geotiff_ll_lat, geotiff_ll_lon, ll_lat,
                                  ll_lon, datum_string.c_str(), "W84");
  datum_convert_ptr->ConvertDatum(geotiff_ur_lat, geotiff_ur_lon, ur_lat,
                                  ur_lon, datum_string.c_str(), "W84");

  // verify scale in image description
  {
    static const struct {
      const char* text;
      double denom;
    } kDrgScales[] = {
        {"USGS GEOTIFF DRG 1:250000", ONE_TO_250K},
        {"USGS GEOTIFF DRG 1:100000", ONE_TO_100K},
        {"USGS GEOTIFF DRG 1:63360", ONE_TO_63360},
        {"USGS GEOTIFF DRG 1:30000", ONE_TO_30K},
        {"USGS GEOTIFF DRG 1:25000", ONE_TO_25K},
        {"USGS GEOTIFF DRG 1:24000", ONE_TO_24K},
        {"USGS GEOTIFF DRG 1:20000", ONE_TO_20K},
    };
    for (size_t i = 0; i < sizeof(kDrgScales) / sizeof(kDrgScales[0]); ++i) {
      if (image_description.find(kDrgScales[i].text) != std::string::npos) {
        *scale_denom = kDrgScales[i].denom;
        *sScaleUnits = MAP_SCALE_DENOMINATOR;
        if (*scale_denom != scale_from_file_name) goto CHECK_DOQ;
        props->series = L"";
        props->supported = true;
        return S_OK;
      }
    }
  }

  goto CHECK_DOQ;

  // check for USGS 1 meter DOQ *******************************************
CHECK_DOQ:
  if (image_description.find("USGS GEOTIFF DIGITAL ORTHOIMAGE") ==
          std::string::npos &&
      image_description.find("USGS GEOTIFF DOQ") == std::string::npos)
    goto CHECK_OTHER;

  // check meters per pixel scale (nominal 1.0) in x and y directions
  if (geodata.m_model_pixel_scale_x != geodata.m_model_pixel_scale_y)
    goto NOT_SUPPORTED;
  if (geodata.m_model_pixel_scale_x != 1.0) goto NOT_SUPPORTED;

  switch (image_type) {
    case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
      *scale_denom = 1;
      *sScaleUnits = MAP_SCALE_METERS;
      props->series = BLACK_AND_WHITE_SERIES;
      if (gtf.m_samples_per_pixel == 3) props->series = COLOR_INFRARED_SERIES;
      break;
    case GEOTIFF_IMAGE_TYPE_256_COLOR:
      *scale_denom = 1;
      *sScaleUnits = MAP_SCALE_METERS;
      props->series = COLOR_INFRARED_SERIES;
      break;
    case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
      *scale_denom = 1;
      *sScaleUnits = MAP_SCALE_METERS;
      props->series = COLOR_INFRARED_SERIES;
      break;
    default:
      goto CHECK_OTHER;
  }

  // determine coverage from file name
  if (gtf.TIF_get_doq_coverage(file_name, geotiff_ll_lat, geotiff_ll_lon,
                               geotiff_ur_lat, geotiff_ur_lon) != SUCCESS)
    goto CHECK_OTHER;

  // convert datum for coverage lat/lon's
  datum_convert_ptr->ConvertDatum(geotiff_ll_lat, geotiff_ll_lon, ll_lat,
                                  ll_lon, datum_string.c_str(), "W84");
  datum_convert_ptr->ConvertDatum(geotiff_ur_lat, geotiff_ur_lon, ur_lat,
                                  ur_lon, datum_string.c_str(), "W84");

  props->supported = true;
  return S_OK;

CHECK_OTHER:

  // determine the coverage
  if (gtf.TIF_determine_coverage(geotiff_ll_lat, geotiff_ll_lon,
                                 geotiff_ur_lat, geotiff_ur_lon) != SUCCESS)
    goto NOT_SUPPORTED;

  // convert datum for coverage lat/lon's
  datum_convert_ptr->ConvertDatum(geotiff_ll_lat, geotiff_ll_lon, ll_lat,
                                  ll_lon, datum_string.c_str(), "W84");
  datum_convert_ptr->ConvertDatum(geotiff_ur_lat, geotiff_ur_lon, ur_lat,
                                  ur_lon, datum_string.c_str(), "W84");

  // determine the image scale and series
  if (gtf.TIF_determine_scale_and_series(*scale_denom, *sScaleUnits,
                                         series_str) == SUCCESS) {
    props->series = series_str;
    props->supported = true;
    return S_OK;
  }

NOT_SUPPORTED:
  // Windows falls back to the ImageLib COM object (MrSID etc.) here; that
  // fallback is not ported, so the frame is reported unsupported.
  props->supported = false;
  return S_OK;
}

}  // namespace fv

// Copyright (c) 1994-2012 Georgia Tech Research Corporation, Atlanta, GA
// This file is part of FalconView(R).

// FalconView(R) is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// FalconView(R) is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public License
// along with FalconView(R).  If not, see <http://www.gnu.org/licenses/>.

// FalconView(R) is a registered trademark of Georgia Tech Research Corporation.

// GeoTiffFrameFile.cpp : Implementation of CGeoTiffFrameFile
#include "stdafx.h"
#include "GeoTiffFrameFile.h"
#include "GeoTiff_defines.h"
#ifdef _WIN32
#include "ComErrorHandler.h"
#include "ComErrorObject.h"
#endif
#include "projsp.h"

/////////////////////////////////////////////////////////////////////////////
// CGeoTiffFrameFile


#ifdef _WIN32  // COM surface; portable twin in port/GeoTIFFMapServer/
STDMETHODIMP CGeoTiffFrameFile::put_m_FileName(BSTR newVal)
{
   m_FileName = std::string(_bstr_t(newVal));

	return S_OK;
}

STDMETHODIMP CGeoTiffFrameFile::put_m_FilePath(BSTR newVal)
{
   m_FilePath = std::string(_bstr_t(newVal));

	return S_OK;
}

// GetFrameProperties - this function checks the .tif file for being a supported geotiff
// file (USGS DRG, DOQ) and if so returns the bounding latitude and longitude,
// file size, scale, and series
//
// This code was originally written by a liberal -- what can I say?

STDMETHODIMP CGeoTiffFrameFile::raw_GetFrameProperties(double *ll_lat, double *ll_lon, double *ur_lat,
   double *ur_lon, double *scale_denom, short *sScaleUnits, BSTR *series, VARIANT_BOOL *frame_supported)
{
   // this function checks the .tif file specified by file_path for being a
   // supported geotiff file (USGS DRG, DOQ) and if so returns the bounding
   // latitude and longitude, file size, scale, and series
   // returns SUCCESS if the file is supported or FAILURE otherwise

	*series = NULL;

   BOOL image_type_supported, geodata_present, geodata_supported;
   int status, error_code, image_type, image_width, image_length;
   std::string image_type_description, error_message;
   std::string path_copy;
   CGeoData geodata;
   std::string image_description;
   std::string datum_string;
   std::wstring series_str;
   IDatumConvertPtr datum_convert_ptr;
   int scale_from_file_name;

   TRY_BLOCK
   {
      CO_CREATE(datum_convert_ptr, __uuidof(DatumConvert));
   } CATCH_BLOCK_RET

   double geotiff_ll_lat, geotiff_ll_lon, geotiff_ur_lat, geotiff_ur_lon;

   // try to load the file with the geotiff class library
   std::string tiff_file_path(m_FilePath + m_FileName);
   status = load(tiff_file_path, image_type_supported,
                          image_type, image_type_description,
                          image_width, image_length,
                          geodata_present, geodata_supported,
                          error_message );

   // return FAILURE if the load failed, the image type is not supported,
   // or if the georeferencing data is not supported
   if( status != SUCCESS || image_width < 3 || image_length < 3 ||
       !image_type_supported || !geodata_supported )
      goto NOT_SUPPORTED;

   // get the geodata structure
   if( get_geodata( geodata, error_code, error_message ) != SUCCESS )
      goto NOT_SUPPORTED;
   // determine datum string
   switch( geodata.m_geog_geodetic_datum )
   {
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
   if( get_image_description( image_description ) != SUCCESS )
      image_description = "";
   if (image_description.length() > 0)
      _tcsupr_s((char *)image_description.c_str(), image_description.capacity() + 1);

   // check for USGS DRG *************************************************
   if( image_description.find("USGS GEOTIFF DRG") == std::string::npos )
      goto CHECK_DOQ;

   // determine scale and coverage from file name
   if( TIF_get_drg_coverage( m_FileName, scale_from_file_name,
       geotiff_ll_lat, geotiff_ll_lon, geotiff_ur_lat, geotiff_ur_lon ) !=
       SUCCESS )
      goto CHECK_DOQ;

   // convert datum for coverage lat/lon's to WGS84
   datum_convert_ptr->ConvertDatum(geotiff_ll_lat, geotiff_ll_lon,
      ll_lat, ll_lon, _bstr_t(datum_string.c_str()), _bstr_t("W84"));

   datum_convert_ptr->ConvertDatum(geotiff_ur_lat, geotiff_ur_lon,
         ur_lat, ur_lon, _bstr_t(datum_string.c_str()), _bstr_t("W84"));

   // verify scale in image description
   if( image_description.find( "USGS GEOTIFF DRG 1:250000" ) != std::string::npos )
   {
      *scale_denom = ONE_TO_250K;
      *sScaleUnits = MAP_SCALE_DENOMINATOR;
      if( *scale_denom != scale_from_file_name ) goto CHECK_DOQ;
      *series = _bstr_t("").copy();
      *frame_supported = VARIANT_TRUE;
      return S_OK;
   }

   if( image_description.find( "USGS GEOTIFF DRG 1:100000" ) != std::string::npos )
   {
      *scale_denom = ONE_TO_100K;
      *sScaleUnits = MAP_SCALE_DENOMINATOR;
      if( *scale_denom != scale_from_file_name ) goto CHECK_DOQ;
      *series = _bstr_t("").copy();
      *frame_supported = VARIANT_TRUE;
      return S_OK;
   }

   if( image_description.find( "USGS GEOTIFF DRG 1:63360" ) != std::string::npos )
   {
      *scale_denom = ONE_TO_63360;
      *sScaleUnits = MAP_SCALE_DENOMINATOR;
      if( *scale_denom != scale_from_file_name ) goto CHECK_DOQ;
      *series = _bstr_t("").copy();
      *frame_supported = VARIANT_TRUE;
      return S_OK;
   }

   if( image_description.find( "USGS GEOTIFF DRG 1:30000" ) != std::string::npos )
   {
      *scale_denom = ONE_TO_30K;
      *sScaleUnits = MAP_SCALE_DENOMINATOR;
      if( *scale_denom != scale_from_file_name ) goto CHECK_DOQ;
      *series = _bstr_t("").copy();
      *frame_supported = VARIANT_TRUE;
      return S_OK;
   }

   if( image_description.find( "USGS GEOTIFF DRG 1:25000" ) != std::string::npos )
   {
      *scale_denom = ONE_TO_25K;
      *sScaleUnits = MAP_SCALE_DENOMINATOR;
      if( *scale_denom != scale_from_file_name ) goto CHECK_DOQ;
      *series = _bstr_t("").copy();
      *frame_supported = VARIANT_TRUE;
      return S_OK;
   }

   if( image_description.find( "USGS GEOTIFF DRG 1:24000" ) != std::string::npos )
   {
      *scale_denom = ONE_TO_24K;
      *sScaleUnits = MAP_SCALE_DENOMINATOR;
      if( *scale_denom != scale_from_file_name ) goto CHECK_DOQ;
      *series = _bstr_t("").copy();
      *frame_supported = VARIANT_TRUE;
      return S_OK;
   }

   if( image_description.find( "USGS GEOTIFF DRG 1:20000" ) != std::string::npos )
   {
      *scale_denom = ONE_TO_20K;
      *sScaleUnits = MAP_SCALE_DENOMINATOR;
      if( *scale_denom != scale_from_file_name ) goto CHECK_DOQ;
      *series = _bstr_t("").copy();
      *frame_supported = VARIANT_TRUE;
      return S_OK;
   }

   goto CHECK_DOQ;

   // check for USGS 1 meter DOQ **********************************************
CHECK_DOQ:
   if( image_description.find( "USGS GEOTIFF DIGITAL ORTHOIMAGE" ) == std::string::npos &&
       image_description.find( "USGS GEOTIFF DOQ" ) == std::string::npos)
      goto CHECK_OTHER;

   // check meters per pixel scale (nominal 1.0) in x and y directions
   if( geodata.m_model_pixel_scale_x != geodata.m_model_pixel_scale_y )
      goto NOT_SUPPORTED;
   if( geodata.m_model_pixel_scale_x != 1.0 )
      goto NOT_SUPPORTED;

   switch( image_type )
   {
      case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
         *scale_denom = 1;
         *sScaleUnits = MAP_SCALE_METERS;
         *series = _bstr_t(BLACK_AND_WHITE_SERIES).copy();
		  if (m_samples_per_pixel == 3)
        {
           ::SysFreeString(*series);
		     *series = _bstr_t(COLOR_INFRARED_SERIES).copy();
        }
         break;
      case GEOTIFF_IMAGE_TYPE_256_COLOR:
         *scale_denom = 1;
         *sScaleUnits = MAP_SCALE_METERS;
         *series = _bstr_t(COLOR_INFRARED_SERIES).copy();
         break;
      case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
         *scale_denom = 1;
         *sScaleUnits = MAP_SCALE_METERS;
         *series = _bstr_t(COLOR_INFRARED_SERIES).copy();
         break;
      default:
         goto CHECK_OTHER;
   }

   // determine coverage from file name
   if( TIF_get_doq_coverage( m_FileName, geotiff_ll_lat, geotiff_ll_lon,
       geotiff_ur_lat, geotiff_ur_lon ) != SUCCESS )
      goto CHECK_OTHER;

   // convert datum for coverage lat/lon's
   datum_convert_ptr->ConvertDatum( geotiff_ll_lat, geotiff_ll_lon,
      ll_lat, ll_lon, _bstr_t(datum_string.c_str()), _bstr_t("W84"));
   datum_convert_ptr->ConvertDatum( geotiff_ur_lat, geotiff_ur_lon,
      ur_lat, ur_lon, _bstr_t(datum_string.c_str()), _bstr_t("W84"));

   *frame_supported = VARIANT_TRUE;
   return S_OK;

CHECK_OTHER:

   // determine the coverage
   if( TIF_determine_coverage( geotiff_ll_lat, geotiff_ll_lon,
      geotiff_ur_lat, geotiff_ur_lon ) != SUCCESS )
      goto NOT_SUPPORTED;

   // convert datum for coverage lat/lon's
   datum_convert_ptr->ConvertDatum(geotiff_ll_lat, geotiff_ll_lon,
      ll_lat, ll_lon, _bstr_t(datum_string.c_str()), _bstr_t("W84"));
   datum_convert_ptr->ConvertDatum( geotiff_ur_lat, geotiff_ur_lon,
      ur_lat, ur_lon, _bstr_t(datum_string.c_str()), _bstr_t("W84"));

   // determine the image scale and series
   if( TIF_determine_scale_and_series( *scale_denom, *sScaleUnits, series_str ) == SUCCESS )
   {
      *series = _bstr_t(series_str.c_str()).copy();
      *frame_supported = VARIANT_TRUE;
      return S_OK;
   }

NOT_SUPPORTED:
   // try ImageLib
	int rslt;
	std::string info;

	if (m_imagelib == NULL)
		HRESULT hr = m_imagelib.CreateInstance(__uuidof(ImageLib));

	rslt = imagelib_load( m_imagelib, tiff_file_path, m_datum_str, m_width, m_height, error_message);

	if (rslt != SUCCESS)
	{
		*frame_supported = VARIANT_FALSE;
		return S_OK;
	}

	*ll_lat = m_ll_lat;
	*ll_lon = m_ll_lon;
	*ur_lat = m_ur_lat;
	*ur_lon = m_ur_lon;

	*sScaleUnits = MAP_SCALE_METERS;

	determine_scale(*scale_denom);
	*series = _bstr_t(m_series_str.c_str()).copy();

   *frame_supported = VARIANT_TRUE;
   return S_OK;
}

// ******************************************************************************

int CGeoTiffFrameFile::determine_scale( double &scale )
{
   // This function attempts to determine the scale and series for the argument
   // CGeoTiff object with image description and software strings
   int hpix, vpix, rslt, berr;
//   int image_width, image_length, image_type;
   double center_lat, center_lon, right_lat, right_lon, above_lat, above_lon;
   double meters_per_pixel;
   double center_x, center_y, center_z;
   double right_x, right_y, right_z;
   double above_x, above_y, above_z;
   double right_dist, above_dist;
   std::string image_description, software;
   _bstr_t berr_msg;

   // determine meters per mixel at the image center
   hpix = m_width / 2;
   vpix = m_height / 2;
   m_imagelib->image_to_geo( hpix, vpix, &center_lat, &center_lon, &berr, berr_msg.GetAddress() );
   if (berr != 0 )
	 return FAILURE;
   rslt = TIF_lat_lon_to_xyz( center_lat, center_lon, center_x, center_y, center_z );
   if (rslt != SUCCESS )
	 return FAILURE;
   m_imagelib->image_to_geo( hpix+1, vpix, &right_lat, &right_lon, &berr, berr_msg.GetAddress() );
   if (berr != 0 )
	 return FAILURE;
   rslt = TIF_lat_lon_to_xyz( right_lat, right_lon, right_x, right_y, right_z );
   if (rslt != SUCCESS )
	 return FAILURE;
   m_imagelib->image_to_geo( hpix, vpix-1, &above_lat, &above_lon, &berr, berr_msg.GetAddress() );
   if (berr != 0 )
	 return FAILURE;
   rslt = TIF_lat_lon_to_xyz( above_lat, above_lon, above_x, above_y, above_z );
   if (rslt != SUCCESS )
	 return FAILURE;
   right_dist = sqrt( (right_x-center_x)*(right_x-center_x) + 
					  (right_y-center_y)*(right_y-center_y) +
					  (right_z-center_z)*(right_z-center_z) );
   above_dist = sqrt( (above_x-center_x)*(above_x-center_x) + 
					  (above_y-center_y)*(above_y-center_y) +
					  (above_z-center_z)*(above_z-center_z) );
   // average vertical and horizontal values
   meters_per_pixel = (right_dist + above_dist) / 2.0;

	// assign to nearest supported resolution
	if( meters_per_pixel > 75.0 )
	{
		scale = 100;
		return SUCCESS;
	}
	if ( meters_per_pixel > 40.0 )
	{
		scale = 50;
		return SUCCESS;
	}
	if ( meters_per_pixel > 25.0 )
	{
		scale = 30;
		return SUCCESS;
	}
	if ( meters_per_pixel > 17.5 )
	{
		scale = 20;
		return SUCCESS;
	}
	if ( meters_per_pixel > 12.5 )
	{
		scale = 15;
		return SUCCESS;
	}
	if ( meters_per_pixel > 7.5 )
	{
		scale = 10;
		return SUCCESS;
	}
	if ( meters_per_pixel > 3.5 )
	{
		scale = 5;
		return SUCCESS;
	}
	if ( meters_per_pixel > 1.5 )
	{
		scale = 2;
		return SUCCESS;
	}
	if ( meters_per_pixel > 0.8 )
	{
		scale = 1;
		return SUCCESS;
	}
	if( meters_per_pixel > 0.55 )
	{
		scale = 0.6;
		return SUCCESS;
	}
	if( meters_per_pixel > 0.4 )
	{
		scale = 0.5;
		return SUCCESS;
	}
	if( meters_per_pixel > 0.225 )
	{
		scale = 0.3;
		return SUCCESS;
	}
	if( meters_per_pixel > 0.112 )
	{
		scale = 0.15;
		return SUCCESS;
	}
	if( meters_per_pixel > 0.0625 )
	{
		scale = 0.075;
		return SUCCESS;
	}
	if( meters_per_pixel > 0.0375 )
	{
		scale = 0.050;
		return SUCCESS;
	}
	if( meters_per_pixel > 0.0185 )
	{
		scale = 0.025;
		return SUCCESS;
	}
	scale = 0.012;
	return SUCCESS;

}

#endif  // _WIN32 (COM surface: setters, raw_GetFrameProperties, determine_scale)
// ******************************************************************************
// ******************************************************************************

#ifdef _WIN32  // BSTR-returning helper, unused off Windows
int CGeoTiffFrameFile::get_series_from_info( std::string info, double *scale_denom, short *sScaleUnits, BSTR *series)
{
	int rslt, spp;
	double scale;
	std::string data;

	rslt = extract_data_from_info(info, "Samples Per Pixel", &data);
	spp = atoi( data.c_str() );

	*scale_denom = 1;
	*sScaleUnits = MAP_SCALE_METERS;
	*series = _bstr_t(BLACK_AND_WHITE_SERIES).copy();
	if (spp > 1)
	{
		::SysFreeString(*series);
		*series = _bstr_t(COLOR_INFRARED_SERIES).copy();
	}

	rslt = extract_data_from_info(info, "Native Pixel Size", &data);
	scale = atof( data.c_str() );

	return SUCCESS;
}

#endif  // _WIN32 (get_series_from_info returns BSTR)
int CGeoTiffFrameFile::extract_data_from_info( std::string info, std::string key, std::string *data)
{
	std::string tstr;
	size_t len, pos;

	*data = "";
	len = info.length();
	pos = info.find(key);
	if ((pos < 0) || (pos >= len))
		return FAILURE;

	if (pos >= 0)
	{
		tstr = info.substr(pos, len - pos);
		len = tstr.length();
		pos = tstr.find(':');
		if ((pos < 0) || (pos >= len))
			return FAILURE;

		len = tstr.length();
		tstr = tstr.substr(pos, len - pos - 2);
		pos = tstr.find('\r');
		if (pos > 0)
			*data = tstr.substr(0, pos);
		else
			*data = tstr;
	}

	return SUCCESS;
}


int CGeoTiffFrameFile::TIF_get_drg_coverage( std::string info, int &scale,
                          double &ll_lat, double &ll_lon,
                          double &ur_lat, double &ur_lon )
{
   // this function determines the scale and lat/lon coverage of a usgs drg
   // file based on the name of the file
   // returns SUCCESS or FAILURE

   int length, ilat, ilon, i100, i10, i1;
   int secondary_cell_lat, secondary_cell_lon;
   double primary_cell_width, primary_cell_height;
   double primary_cell_lat, primary_cell_lon;
   char c, string[2], primary_cell_c1, primary_cell_c2;

   // make the file name uppercase
   if (m_tiff_file_name.length() > 0)
      _tcsupr_s((char *)m_tiff_file_name.c_str(), m_tiff_file_name.capacity() + 1);

   // make sure that the file name is 12 characters long
   length = m_tiff_file_name.size( );
   if( length != 12 )
      goto FAIL;

   // check first character to determine scale
   c = m_tiff_file_name[0];
   switch( c )
   {
      case 'C':                        // USGS 1:250K DRG  2deg x 1deg
         scale = ONE_TO_250K;
         primary_cell_width = 2.0;
         primary_cell_height = 1.0;
         break;
      case 'F':                        // USGS 1:100K DRG  1deg x 0.5deg
         scale = ONE_TO_100K;
         primary_cell_width = 1.0;
         primary_cell_height = 0.5;
         break;
      case 'I':                        // USGS 1:63360 DRG (Alaska)  22.5'x15'
         scale = ONE_TO_63360;
         primary_cell_width = 0.375;
         primary_cell_height = 0.25;
         break;
      case 'J':                        // USGS 1:30K DRG
         // only 2 of these 1:30000 scale "J" files exist and they don't fit
         // the standard naming convention - so we just hardwire the corners
         // of the two known files, which are islands near Puerto Rico
         if( m_tiff_file_name.find( "J18065C2" ) != std::string::npos )    // known file
         {                          // "CULEBRA AND ADJACENT ISLANDS"
            scale = ONE_TO_30K;
            ll_lat = 18.266666;
            ll_lon = -65.4;
            ur_lat = 18.366666;
            ur_lon = -65.216666;
            return SUCCESS;
         }
         if( m_tiff_file_name.find( "J18065A3" ) != std::string::npos )    // known file
         {                          // "ISLA DE VIEQUES"
            scale = ONE_TO_30K;
            ll_lat = 18.066666;
            ll_lon = -65.583333;
            ur_lat = 18.175;
            ur_lon = -65.266666;
            return SUCCESS;
         }
         // otherwise the file is unknown
         goto FAIL;
      case 'K':                        // USGS 1:25K DRG  15'x7.5'
         scale = ONE_TO_25K;
         primary_cell_width = 0.25;
         primary_cell_height = 0.125;
         break;
      case 'L':                        // USGS 1:25K DRG  7.5'x7.5'
         scale = ONE_TO_25K;
         primary_cell_width = 0.125;
         primary_cell_height = 0.125;
         break;
      case 'O':                        // USGS 1:24K DRG  7.5'x7.5'
         scale = ONE_TO_24K;
         primary_cell_width = 0.125;
         primary_cell_height = 0.125;
         break;
      case 'P':                        // USGS 1:24K DRG  7.5'x7.5'
         scale = ONE_TO_24K;
         primary_cell_width = 0.125;
         primary_cell_height = 0.125;
         break;
      case 'R':                        // USGS 1:20K DRG  7.5'x7.5'
         scale = ONE_TO_20K;
         primary_cell_width = 0.125;
         primary_cell_height = 0.125;
         break;
      default:
         goto FAIL;
   }

   // determine secondary cell latitude
   string[1] = NULL;
   string[0] = m_tiff_file_name[1];
   if( sscanf_s( string, "%i", &i10 ) != 1 )
      goto FAIL;
   string[0] = m_tiff_file_name[2];
   if( sscanf_s( string, "%i", &i1 ) != 1 )
      goto FAIL;
   secondary_cell_lat = 10*i10 + i1;
   if( secondary_cell_lat >= 90 )
      goto FAIL;

   // determine secondary cell longitude (west is positive 0 to +360 deg)
   string[0] = m_tiff_file_name[3];
   if( sscanf_s( string, "%i", &i100 ) != 1 )
      goto FAIL;
   string[0] = m_tiff_file_name[4];
   if( sscanf_s( string, "%i", &i10 ) != 1 )
      goto FAIL;
   string[0] = m_tiff_file_name[5];
   if( sscanf_s( string, "%i", &i1 ) != 1 )
      goto FAIL;
   secondary_cell_lon = 100*i100 + 10*i10 + i1;
   if( secondary_cell_lat >= 360 )
      goto FAIL;

   // determine primary cell
   primary_cell_c1 = m_tiff_file_name[6];
   primary_cell_c2 = m_tiff_file_name[7];

   switch( primary_cell_c1 )
   {
      case 'A':
         ilat = 0;
         break;
      case 'B':
         ilat = 1;
         break;
      case 'C':
         ilat = 2;
         break;
      case 'D':
         ilat = 3;
         break;
      case 'E':
         ilat = 4;
         break;
      case 'F':
         ilat = 5;
         break;
      case 'G':
         ilat = 6;
         break;
      case 'H':
         ilat = 7;
         break;
      default:
         goto FAIL;
   }

   switch( primary_cell_c2 )
   {
      case '1':
         ilon = 0;
         break;
      case '2':
         ilon = 1;
         break;
      case '3':
         ilon = 2;
         break;
      case '4':
         ilon = 3;
         break;
      case '5':
         ilon = 4;
         break;
      case '6':
         ilon = 5;
         break;
      case '7':
         ilon = 6;
         break;
      case '8':
         ilon = 7;
         break;
      default:
         goto FAIL;
   }

   // find the lat/lon of the primary cell lower right corner
   primary_cell_lat = secondary_cell_lat + ilat * 0.125;
   primary_cell_lon = secondary_cell_lon + ilon * 0.125;

   // calculate lower left and upper right corner lat/lon's
   ll_lat = primary_cell_lat;
   ll_lon = primary_cell_lon + primary_cell_width;

   ur_lat = primary_cell_lat + primary_cell_height;
   ur_lon = primary_cell_lon;

   // now convert to east positive 0 to 180, west negative 0 to -180
   ll_lon = -ll_lon;
   if( ll_lon < -180.0 )
      ll_lon += 360.0;
   ur_lon = -ur_lon;
   if( ur_lon < -180.0 )
      ur_lon += 360.0;

   return SUCCESS;

FAIL:
   return FAILURE;
}

int CGeoTiffFrameFile::TIF_get_doq_coverage( std::string tiff_file_name,
                          double &ll_lat, double &ll_lon,
                          double &ur_lat, double &ur_lon )
{
   // this function determines the lat/lon coverage of a usgs doq
   // file based on the name of the file
   // returns SUCCESS or FAILURE

   int length, ilat, ilon, i100, i10, i1;
   int secondary_cell_lat, secondary_cell_lon;
   double primary_cell_lat, primary_cell_lon;
   char string[2], primary_cell_c1, primary_cell_c2, primary_cell_c3;

   // make the file name uppercase
   if (tiff_file_name.length() > 0)
      _tcsupr_s((char *)tiff_file_name.c_str(), tiff_file_name.capacity() + 1);

   // make sure that the file name is 12 characters long
   length = tiff_file_name.size( );
   if( length != 12 )
      goto FAIL;

   // determine secondary cell latitude
   string[1] = NULL;
   string[0] = tiff_file_name[0];
   if( sscanf_s( string, "%i", &i10 ) != 1 )
      goto FAIL;
   string[0] = tiff_file_name[1];
   if( sscanf_s( string, "%i", &i1 ) != 1 )
      goto FAIL;
   secondary_cell_lat = 10*i10 + i1;
   if( secondary_cell_lat >= 90 )
      goto FAIL;

   // determine secondary cell longitude (west is positive 0 to +360 deg)
   string[0] = tiff_file_name[2];
   if( sscanf_s( string, "%i", &i100 ) != 1 )
      goto FAIL;
   string[0] = tiff_file_name[3];
   if( sscanf_s( string, "%i", &i10 ) != 1 )
      goto FAIL;
   string[0] = tiff_file_name[4];
   if( sscanf_s( string, "%i", &i1 ) != 1 )
      goto FAIL;
   secondary_cell_lon = 100*i100 + 10*i10 + i1;
   if( secondary_cell_lat >= 360 )
      goto FAIL;

   // determine primary cell
   primary_cell_c1 = tiff_file_name[5];
   primary_cell_c2 = tiff_file_name[6];
   primary_cell_c3 = tiff_file_name[7];

   switch( primary_cell_c1 )
   {
      case 'A':
         ilat = 0;
         break;
      case 'B':
         ilat = 1;
         break;
      case 'C':
         ilat = 2;
         break;
      case 'D':
         ilat = 3;
         break;
      case 'E':
         ilat = 4;
         break;
      case 'F':
         ilat = 5;
         break;
      case 'G':
         ilat = 6;
         break;
      case 'H':
         ilat = 7;
         break;
      default:
         goto FAIL;
   }

   switch( primary_cell_c2 )
   {
      case '1':
         ilon = 0;
         break;
      case '2':
         ilon = 1;
         break;
      case '3':
         ilon = 2;
         break;
      case '4':
         ilon = 3;
         break;
      case '5':
         ilon = 4;
         break;
      case '6':
         ilon = 5;
         break;
      case '7':
         ilon = 6;
         break;
      case '8':
         ilon = 7;
         break;
      default:
         goto FAIL;
   }

   // find the lat/lon of the primary cell lower right corner
   primary_cell_lat = secondary_cell_lat + ilat * 0.125;
   primary_cell_lon = secondary_cell_lon + ilon * 0.125;

   // now adjust for the required quarter quad
   switch( primary_cell_c3 )
   {
      case '1':
         primary_cell_lat += 0.0625;
         primary_cell_lon += 0.0625;
         break;
      case '2':
         primary_cell_lat += 0.0625;
         break;
      case '3':
         break;
      case '4':
         primary_cell_lon += 0.0625;
         break;
      case '5':
         primary_cell_lat += 0.0625;
         primary_cell_lon += 0.0625;
         break;
      case '6':
         primary_cell_lat += 0.0625;
         break;
      case '7':
         break;
      case '8':
         primary_cell_lon += 0.0625;
         break;
      default:
         goto FAIL;
   }

   // calculate lower left and upper right corner lat/lon's
   ll_lat = primary_cell_lat;
   ll_lon = primary_cell_lon + 0.0625;

   ur_lat = primary_cell_lat + 0.0625;
   ur_lon = primary_cell_lon;

   // now convert to east positive 0 to 180, west negative 0 to -180
   ll_lon = -ll_lon;
   if( ll_lon < -180.0 )
      ll_lon += 360.0;
   ur_lon = -ur_lon;
   if( ur_lon < -180.0 )
      ur_lon += 360.0;

   return SUCCESS;

FAIL:
   return FAILURE;
}

int CGeoTiffFrameFile::TIF_determine_coverage( double &ll_lat, double &ll_lon,
                            double &ur_lat, double &ur_lon )
{
   // this function determines the coverage georect for the geotiff file.
   // the datum is the geotiff file's datum

   int index;
   double lat_upper_left, long_upper_left, lat_lower_left, long_lower_left;
   double lat_lower_right, long_lower_right, lat_upper_right, long_upper_right;
   std::string image_description;

   // get the image description
   if( get_image_description( image_description ) != SUCCESS )
      image_description = "";
   if (image_description.length() > 0)
      _tcsupr_s((char *)image_description.c_str(), image_description.capacity() + 1);

   // see if the image description includes the coverage georect
   index = image_description.find( "GEORECT" );
   if( index != std::string::npos )
   {
      index += 7;
      image_description = image_description.substr(index, image_description.size() - index);
      if( sscanf_s( image_description.c_str(), "%lf%lf%lf%lf",
         &ll_lat, &ll_lon, &ur_lat, &ur_lon )
         == 4 )
      {
         // check for valid lat/lon values
         if(
            ll_lat >= -90.0 && ll_lat <= 90.0 && ll_lat < ur_lat &&
            ur_lat >= -90.0 && ur_lat <= 90.0 &&
            ll_lon >= -180.0 && ll_lon < 180.0 && ll_lon < ur_lon &&
            ur_lon >= -180.0 && ur_lon <= 180.0
         )
            return SUCCESS;
      }
   }

   // otherwise, get the coordinates of the four corners and use max/min
   if( get_image_bounds( lat_upper_left, long_upper_left,
       lat_lower_left, long_lower_left, lat_lower_right, long_lower_right,
       lat_upper_right, long_upper_right ) != SUCCESS )
       return FAILURE;

   ll_lat = __min( lat_upper_left, lat_lower_left );
   ll_lat = __min( ll_lat, lat_lower_right );
   ll_lat = __min( ll_lat, lat_upper_right );

   ll_lon = __min( long_upper_left, long_lower_left );
   ll_lon = __min( ll_lon, long_lower_right );
   ll_lon = __min( ll_lon, long_upper_right );

   ur_lat = __max( lat_upper_left, lat_lower_left );
   ur_lat = __max( ur_lat, lat_lower_right );
   ur_lat = __max( ur_lat, lat_upper_right );

   ur_lon = __max( long_upper_left, long_lower_left );
   ur_lon = __max( ur_lon, long_lower_right );
   ur_lon = __max( ur_lon, long_upper_right );

   return SUCCESS;
}

int CGeoTiffFrameFile::TIF_determine_scale_and_series( double &scale, short &sScaleUnits, std::wstring &series )
{
   // This function attempts to determine the scale and series for the argument
   // CGeoTiff object with image description and software strings
   int hpix, vpix, rslt;
   int image_width, image_length, image_type;
   double center_lat, center_lon, right_lat, right_lon, above_lat, above_lon;
   double meters_per_pixel;
   double center_x, center_y, center_z;
   double right_x, right_y, right_z;
   double above_x, above_y, above_z;
   double right_dist, above_dist;
   std::string image_description, software;

   // get the image description
   if( get_image_description( image_description ) != SUCCESS )
      image_description = "";
   if (image_description.length() > 0)
      _tcsupr_s((char *)image_description.c_str(), image_description.capacity() + 1);

   // First, check the image description.
   if( TIF_check_image_description( image_description, scale, series ) ==
       SUCCESS )
   {
      sScaleUnits = MAP_SCALE_DENOMINATOR;
      return SUCCESS;
   }

   // get the image width, length, and type
   rslt = get_image_width( image_width );
   if (rslt != SUCCESS )
      return FAILURE;
   rslt = get_image_length( image_length );
   if (rslt != SUCCESS )
      return FAILURE;
   rslt = get_image_type( image_type );
   if (rslt != SUCCESS )
      return FAILURE;

   // determine meters per mixel at the image center
   hpix = image_width / 2;
   vpix = image_length / 2;
   rslt = inv_transform( hpix, vpix, center_lat, center_lon );
   if (rslt != SUCCESS )
      goto NOT_SUPPORTED;
   rslt = TIF_lat_lon_to_xyz( center_lat, center_lon, center_x, center_y, center_z );
   if (rslt != SUCCESS )
      goto NOT_SUPPORTED;
   rslt = inv_transform( hpix+1, vpix, right_lat, right_lon );
   if (rslt != SUCCESS )
      goto NOT_SUPPORTED;
   rslt = TIF_lat_lon_to_xyz( right_lat, right_lon, right_x, right_y, right_z );
   if (rslt != SUCCESS )
      goto NOT_SUPPORTED;
   rslt = inv_transform( hpix, vpix-1, above_lat, above_lon );
   if (rslt != SUCCESS )
      goto NOT_SUPPORTED;
   rslt = TIF_lat_lon_to_xyz( above_lat, above_lon, above_x, above_y, above_z );
   if (rslt != SUCCESS )
	  goto NOT_SUPPORTED;
   right_dist = sqrt( (right_x-center_x)*(right_x-center_x) +
					  (right_y-center_y)*(right_y-center_y) +
					  (right_z-center_z)*(right_z-center_z) );
   above_dist = sqrt( (above_x-center_x)*(above_x-center_x) +
					  (above_y-center_y)*(above_y-center_y) +
					  (above_z-center_z)*(above_z-center_z) );
   // average vertical and horizontal values
   meters_per_pixel = (right_dist + above_dist) / 2.0;

   // check image description for "MAP"
   if (( image_description.find( "MAP " ) != std::string::npos ) || (meters_per_pixel > 125.0))
   {
      // seris is NULL_SERIES for maps
      series = L"";

      // assign to nearest supported scale
		// comparisons break at midpoint

//      if( meters_per_pixel > 5689.6 )
//      {
//         scale = ONE_TO_80M;
//         return SUCCESS;
//      }
      if( meters_per_pixel > 2844.8 )
      {
         scale = ONE_TO_40M;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 1422.4 )
      {
         scale = ONE_TO_20M;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 711.2 )
      {
         scale = ONE_TO_10M;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 355.6 )
      {
         scale = ONE_TO_5M;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 152.4 )
      {
         scale = ONE_TO_2M;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 76.2 )
      {
         scale = ONE_TO_1M;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 38.1 )
      {
         scale = ONE_TO_500K;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 17.78 )
      {
         scale = ONE_TO_250K;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 8.298688 )
      {
         scale = ONE_TO_100K;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 5.758688 )
      {
         scale = ONE_TO_63360;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 4.064 )
      {
         scale = ONE_TO_50K;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 2.794 )
      {
         scale = ONE_TO_30K;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 2.4892 )
      {
         scale = ONE_TO_25K;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 2.2352 )
      {
         scale = ONE_TO_24K;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 1.524 )
      {
         scale = ONE_TO_20K;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 0.762 )
      {
         scale = ONE_TO_10K;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 0.3556 )
      {
         scale = ONE_TO_5K;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      if( meters_per_pixel > 0.1524 )
      {
         scale = ONE_TO_2K;
         sScaleUnits = MAP_SCALE_DENOMINATOR;
         return SUCCESS;
      }
      scale = ONE_TO_1K;
      sScaleUnits = MAP_SCALE_DENOMINATOR;
      return SUCCESS;
   }
   else
   {
      // determine series from image type
      switch( image_type )
      {
         case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
         case GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE:
            series = BLACK_AND_WHITE_SERIES;
			if (m_samples_per_pixel == 3)  // check for multispectral image
			   series = COLOR_SERIES;
            break;
         case GEOTIFF_IMAGE_TYPE_256_COLOR:
         case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
            series = COLOR_SERIES;
            break;
         default:
            goto NOT_SUPPORTED;
      }

      // assign to nearest supported resolution
      if( meters_per_pixel > 75.0 )
      {
         scale = 100;
         sScaleUnits = MAP_SCALE_METERS;
         return SUCCESS;
      }
		if ( meters_per_pixel > 40.0 )
		{
			scale = 50;
			 sScaleUnits = MAP_SCALE_METERS;
			return SUCCESS;
		}
		if ( meters_per_pixel > 25.0 )
		{
			scale = 30;
			 sScaleUnits = MAP_SCALE_METERS;
			return SUCCESS;
		}
 		if ( meters_per_pixel > 17.5 )
		{
			scale = 20;
			 sScaleUnits = MAP_SCALE_METERS;
			return SUCCESS;
		}
		if ( meters_per_pixel > 12.5 )
		{
			scale = 15;
			 sScaleUnits = MAP_SCALE_METERS;
			return SUCCESS;
		}
      if( meters_per_pixel > 7.5 )
      {
         scale = 10;
         sScaleUnits = MAP_SCALE_METERS;
         return SUCCESS;
      }
      if( meters_per_pixel > 3.5 )
      {
         scale = 5;
         sScaleUnits = MAP_SCALE_METERS;
         return SUCCESS;
      }
      if( meters_per_pixel > 1.5 )
      {
         scale = 2;
         sScaleUnits = MAP_SCALE_METERS;
         return SUCCESS;
      }
      if( meters_per_pixel > 0.8 )
	  {
		  scale = 1;
        sScaleUnits = MAP_SCALE_METERS;
		  return SUCCESS;
	  }
      if( meters_per_pixel > 0.55 )
	  {
		  scale = 0.6;
        sScaleUnits = MAP_SCALE_METERS;
		  return SUCCESS;
	  }
      if( meters_per_pixel > 0.4 )
	  {
		  scale = 0.5;
        sScaleUnits = MAP_SCALE_METERS;
		  return SUCCESS;
	  }
      if( meters_per_pixel > 0.225 )
	  {
		  scale = 0.3;
        sScaleUnits = MAP_SCALE_METERS;
		  return SUCCESS;
	  }
      if( meters_per_pixel > 0.112 )
	  {
		  scale = 0.15;
        sScaleUnits = MAP_SCALE_METERS;
		  return SUCCESS;
	  }
      if( meters_per_pixel > 0.0625 )
	  {
		  scale = 0.075;
        sScaleUnits = MAP_SCALE_METERS;
		  return SUCCESS;
	  }
      if( meters_per_pixel > 0.0375 )
	  {
		  scale = 0.050;
        sScaleUnits = MAP_SCALE_METERS;
		  return SUCCESS;
	  }
      if( meters_per_pixel > 0.0185 )
	  {
		  scale = 0.025;
        sScaleUnits = MAP_SCALE_METERS;
		  return SUCCESS;
	  }
	  scale = 0.012;
     sScaleUnits = MAP_SCALE_METERS;
	  return SUCCESS;

   }

NOT_SUPPORTED:
   return FAILURE;
}

int CGeoTiffFrameFile::TIF_lat_lon_to_xyz( double latitude, double longitude,
                        double &x, double &y, double &z )
{
   // This function returns the (x,y,z) coordinates in meters corresponding to
   // the argument latitude and longitude. This is useful for calculating
   // distances. The (x,y,z) origin is at the Earth's center with the X-axis
   // passing through lat=lon=0.0 and the Z-axis passing through the north pole.
   double a = 6378137.0;
   double b = 6356752.3;
   double pi, r;
   double phi, sin_phi, cos_phi;
   double lambda, sin_lambda, cos_lambda;

   if( latitude < -90.0 || latitude > 90.0 || longitude < -180.0 ||
       longitude > 180.0 )
      return FAILURE;

   pi = 4.0 * atan( 1.0 );

   phi = latitude * pi / 180;
   sin_phi = sin( phi );
   cos_phi = cos( phi );
   lambda = longitude * pi / 180.0;
   sin_lambda = sin( lambda );
   cos_lambda = cos( lambda );
   r = sqrt( a*a*b*b /( a*a*sin_phi*sin_phi + b*b*cos_phi*cos_phi ) );
   x = r * cos_phi * cos_lambda;
   y = r * cos_phi * sin_lambda;
   z = r * sin_phi;

   return SUCCESS;
}

int CGeoTiffFrameFile::TIF_check_image_description( std::string &image_description,
                                                   double &scale, std::wstring &series )
{
   // This function checks the image description string to see if the GeoTIFF
   // file is a map and, if possible, determines the scale and series.
   // Retursn SUCCESS if it is able to determine scale and series or FALSE
   // otherwise.

   int i, scale_index, iscale, length;
   BOOL done;
   char c;

   length = image_description.size();

   // check image description string to see if it is a map
   if( image_description.find( "MAP " ) != std::string::npos )
   {
      // assume it is a map
      series = L"";

      // look for map scale in image description "1:250K" or "1:63360", etc.
      scale_index = image_description.find( "1:" );
      if( scale_index != std::string::npos )
      {
         iscale = 0;
         done = FALSE;
         for( i = scale_index + 2; i < length; i++ )
         {
            c = image_description[i];
            switch( c )
            {
               case '0':
                  iscale *= 10;
                  break;
               case '1':
                  iscale *= 10;
                  iscale += 1;
                  break;
               case '2':
                  iscale *= 10;
                  iscale += 2;
                  break;
               case '3':
                  iscale *= 10;
                  iscale += 3;
                  break;
               case '4':
                  iscale *= 10;
                  iscale += 4;
                  break;
               case '5':
                  iscale *= 10;
                  iscale += 5;
                  break;
               case '6':
                  iscale *= 10;
                  iscale += 6;
                  break;
               case '7':
                  iscale *= 10;
                  iscale += 7;
                  break;
               case '8':
                  iscale *= 10;
                  iscale += 8;
                  break;
               case '9':
                  iscale *= 10;
                  iscale += 9;
                  break;
               case 'K':
                  iscale *= 1000;
                  done = TRUE;
                  break;
               case 'M':
                  iscale *= 1000000;
                  done = TRUE;
                  break;
               default:
                  done = TRUE;
                  break;
            }
            if( done )
               break;
         }
         if( iscale > 0 )
         {
            if( iscale > 60000000 )
            {
               scale = ONE_TO_80M;
               return SUCCESS;
            }
            if( iscale > 30000000 )
            {
               scale = ONE_TO_40M;
               return SUCCESS;
            }
            if( iscale > 15000000 )
            {
               scale = ONE_TO_20M;
               return SUCCESS;
            }
            if( iscale > 7500000 )
            {
               scale = ONE_TO_10M;
               return SUCCESS;
            }
            if( iscale > 3750000 )
            {
               scale = ONE_TO_5M;
               return SUCCESS;
            }
            if( iscale > 1500000 )
            {
               scale = ONE_TO_2M;
               return SUCCESS;
            }
            if( iscale > 750000 )
            {
               scale = ONE_TO_1M;
               return SUCCESS;
            }
            if( iscale > 375000 )
            {
               scale = ONE_TO_500K;
               return SUCCESS;
            }
            if( iscale > 175000 )
            {
               scale = ONE_TO_250K;
               return SUCCESS;
            }
            if( iscale > 81680 )
            {
               scale = ONE_TO_100K;
               return SUCCESS;
            }
            if( iscale > 56680 )
            {
               scale = ONE_TO_63360;
               return SUCCESS;
            }
            if( iscale > 40000 )
            {
               scale = ONE_TO_50K;
               return SUCCESS;
            }
            if( iscale > 27500 )
            {
               scale = ONE_TO_30K;
               return SUCCESS;
            }
            if( iscale > 24500 )
            {
               scale = ONE_TO_25K;
               return SUCCESS;
            }
            if( iscale > 22000 )
            {
               scale = ONE_TO_24K;
               return SUCCESS;
            }
            if( iscale > 15000 )
            {
               scale = ONE_TO_20K;
               return SUCCESS;
            }
            if( iscale > 7500 )
            {
               scale = ONE_TO_10K;
               return SUCCESS;
            }
            if( iscale > 3500 )
            {
               scale = ONE_TO_5K;
               return SUCCESS;
            }
            if( iscale > 1500 )
            {
               scale = ONE_TO_2K;
               return SUCCESS;
            }
            scale = ONE_TO_1K;
            return SUCCESS;
         }
      }
      else
         return FAILURE;
   }

   return FAILURE;
}

// CGeoData class:
gtff::CGeoData::CGeoData( )
{
   m_model_tie_point_hpix = NULL;
   m_model_tie_point_vpix = NULL;
   m_model_tie_point_zpix = NULL;
   m_model_tie_point_x = NULL;
   m_model_tie_point_y = NULL;
   m_model_tie_point_z = NULL;

   clear( );
}

// *****************************************************************
// *****************************************************************

gtff::CGeoData::~CGeoData( )
{
   clear( );
}

// *****************************************************************
// *****************************************************************

void gtff::CGeoData::clear( )
{
   // this function clears the geodata
   m_model_pixel_scale_x = 0.0;
   m_model_pixel_scale_y = 0.0;
   m_model_pixel_scale_z = 0.0;

   m_num_tie_points = 0;
   if( m_model_tie_point_hpix != NULL )
   {
      delete m_model_tie_point_hpix;
      m_model_tie_point_hpix = NULL;
   }
   if( m_model_tie_point_vpix != NULL )
   {
      delete m_model_tie_point_vpix;
      m_model_tie_point_vpix = NULL;
   }
   if( m_model_tie_point_zpix != NULL )
   {
      delete m_model_tie_point_zpix;
      m_model_tie_point_zpix = NULL;
   }
   if( m_model_tie_point_x != NULL )
   {
      delete m_model_tie_point_x;
      m_model_tie_point_x = NULL;
   }
   if( m_model_tie_point_y != NULL )
   {
      delete m_model_tie_point_y;
      m_model_tie_point_y = NULL;
   }
   if( m_model_tie_point_z != NULL )
   {
      delete m_model_tie_point_z;
      m_model_tie_point_z = NULL;
   }
   m_gt_model_type = 0;
   m_gt_raster_type = 0;
   m_gt_citation = "";
   m_geographic_type = 0;
   m_geog_citation = "";
   m_geog_geodetic_datum = 0;
   m_geog_prime_meridian = 0;
   m_geog_prime_meridian_long = 0.0;
   m_geog_ellipsoid = 0;
   m_geog_semi_major_axis = 0.0;
   m_geog_semi_minor_axis = 0.0;
   m_geog_inv_flattening = 0.0;
   m_projected_cs_type = 0;
   m_pcs_citation = "";
   m_projection = 0;
   m_proj_coord_trans = 0;
   m_proj_std_parallel_1 = 0.0;
   m_proj_std_parallel_2 = 0.0;
   m_proj_nat_origin_long = 0.0;
   m_proj_nat_origin_lat = 0.0;
   m_proj_false_easting = 0.0;
   m_proj_false_northing = 0.0;
   m_proj_false_origin_long = 0.0;
   m_proj_false_origin_lat = 0.0;
   m_proj_false_origin_easting = 0.0;
   m_proj_false_origin_northing = 0.0;
   m_proj_center_long = 0.0;
   m_proj_center_lat = 0.0;
   m_proj_center_easting = 0.0;
   m_proj_center_northing = 0.0;
   m_proj_scale_at_nat_origin = 0.0;
   m_proj_scale_at_center = 0.0;
   m_proj_azimuth_angle = 0.0;
   m_proj_straight_vert_pole_long = 0.0;
   m_vertical_cs_type = 0;
   m_vertical_citation = "";
   m_vertical_datum = 0;

   m_model_pixel_scale_present = FALSE;
   m_model_tie_point_present = FALSE;
   m_gt_model_type_present = FALSE;
   m_gt_raster_type_present = FALSE;
   m_gt_citation_present = FALSE;
   m_geographic_type_present = FALSE;
   m_geog_citation_present = FALSE;
   m_geog_geodetic_datum_present = FALSE;
   m_geog_prime_meridian_present = FALSE;
   m_geog_prime_meridian_long_present = FALSE;
   m_geog_ellipsoid_present = FALSE;
   m_geog_semi_major_axis_present = FALSE;
   m_geog_semi_minor_axis_present = FALSE;
   m_geog_inv_flattening_present = FALSE;
   m_projected_cs_type_present = FALSE;
   m_pcs_citation_present = FALSE;
   m_projection_present = FALSE;
   m_proj_coord_trans_present = FALSE;
   m_proj_std_parallel_1_present = FALSE;
   m_proj_std_parallel_2_present = FALSE;
   m_proj_nat_origin_long_present = FALSE;
   m_proj_nat_origin_lat_present = FALSE;
   m_proj_false_easting_present = FALSE;
   m_proj_false_northing_present = FALSE;
   m_proj_false_origin_long_present = FALSE;
   m_proj_false_origin_lat_present = FALSE;
   m_proj_false_origin_easting_present = FALSE;
   m_proj_false_origin_northing_present = FALSE;
   m_proj_center_long_present = FALSE;
   m_proj_center_lat_present = FALSE;
   m_proj_center_easting_present = FALSE;
   m_proj_center_northing_present = FALSE;
   m_proj_scale_at_nat_origin_present = FALSE;
   m_proj_scale_at_center_present = FALSE;
   m_proj_azimuth_angle_present = FALSE;
   m_proj_straight_vert_pole_long_present = FALSE;
   m_vertical_cs_type_present = FALSE;
   m_vertical_citation_present = FALSE;
   m_vertical_datum_present = FALSE;
}

// *****************************************************************
// *****************************************************************

void gtff::CGeoData::operator=( CGeoData& geodata )
{
   // this function allows you to copy one CGeoData object to another,
   // being careful to allocate new memory for the tiepoint arrays
   int i;

   // first, clear this CGeoData object
   clear( );

   // now start copying values
   m_model_pixel_scale_x = geodata.m_model_pixel_scale_x;
   m_model_pixel_scale_y = geodata.m_model_pixel_scale_y;
   m_model_pixel_scale_z = geodata.m_model_pixel_scale_z;

   m_num_tie_points = geodata.m_num_tie_points;
   if( m_num_tie_points > 0 )
   {
      // allocate memory for tiepoints
      m_model_tie_point_hpix = new double[m_num_tie_points];
      if( m_model_tie_point_hpix == NULL )
         goto FAIL;
      m_model_tie_point_vpix = new double[m_num_tie_points];
      if( m_model_tie_point_vpix == NULL )
         goto FAIL;
      m_model_tie_point_zpix = new double[m_num_tie_points];
      if( m_model_tie_point_zpix == NULL )
         goto FAIL;
      m_model_tie_point_x = new double[m_num_tie_points];
      if( m_model_tie_point_x == NULL )
         goto FAIL;
      m_model_tie_point_y = new double[m_num_tie_points];
      if( m_model_tie_point_y == NULL )
         goto FAIL;
      m_model_tie_point_z = new double[m_num_tie_points];
      if( m_model_tie_point_z == NULL )
         goto FAIL;
      // copy the tie point coordinates
      for( i = 0; i < m_num_tie_points; i++ )
      {
         m_model_tie_point_hpix[i] = geodata.m_model_tie_point_hpix[i];
         m_model_tie_point_vpix[i] = geodata.m_model_tie_point_vpix[i];
         m_model_tie_point_zpix[i] = geodata.m_model_tie_point_zpix[i];
         m_model_tie_point_x[i] = geodata.m_model_tie_point_x[i];
         m_model_tie_point_y[i] = geodata.m_model_tie_point_y[i];
         m_model_tie_point_z[i] = geodata.m_model_tie_point_z[i];
      }
   }

   m_gt_model_type = geodata.m_gt_model_type;
   m_gt_raster_type = geodata.m_gt_raster_type;
   m_gt_citation = geodata.m_gt_citation;
   m_geographic_type = geodata.m_geographic_type;
   m_geog_citation = geodata.m_geog_citation;
   m_geog_geodetic_datum = geodata.m_geog_geodetic_datum;
   m_geog_prime_meridian = geodata.m_geog_prime_meridian;
   m_geog_prime_meridian_long = geodata.m_geog_prime_meridian_long;
   m_geog_ellipsoid = geodata.m_geog_ellipsoid;
   m_geog_semi_major_axis = geodata.m_geog_semi_major_axis;
   m_geog_semi_minor_axis = geodata.m_geog_semi_minor_axis;
   m_geog_inv_flattening = geodata.m_geog_inv_flattening;
   m_projected_cs_type = geodata.m_projected_cs_type;
   m_pcs_citation = geodata.m_pcs_citation;
   m_projection = geodata.m_projection;
   m_proj_coord_trans = geodata.m_proj_coord_trans;
   m_proj_std_parallel_1 = geodata.m_proj_std_parallel_1;
   m_proj_std_parallel_2 = geodata.m_proj_std_parallel_2;
   m_proj_nat_origin_long = geodata.m_proj_nat_origin_long;
   m_proj_nat_origin_lat = geodata.m_proj_nat_origin_lat;
   m_proj_false_easting = geodata.m_proj_false_easting;
   m_proj_false_northing = geodata.m_proj_false_northing;
   m_proj_false_origin_long = geodata.m_proj_false_origin_long;
   m_proj_false_origin_lat = geodata.m_proj_false_origin_lat;
   m_proj_false_origin_easting = geodata.m_proj_false_origin_easting;
   m_proj_false_origin_northing = geodata.m_proj_false_origin_northing;
   m_proj_center_long = geodata.m_proj_center_long;
   m_proj_center_lat = geodata.m_proj_center_lat;
   m_proj_center_easting = geodata.m_proj_center_easting;
   m_proj_center_northing = geodata.m_proj_center_northing;
   m_proj_scale_at_nat_origin = geodata.m_proj_scale_at_nat_origin;
   m_proj_scale_at_center = geodata.m_proj_scale_at_center;
   m_proj_azimuth_angle = geodata.m_proj_azimuth_angle;
   m_proj_straight_vert_pole_long = geodata.m_proj_straight_vert_pole_long;
   m_vertical_cs_type = geodata.m_vertical_cs_type;
   m_vertical_citation = geodata.m_vertical_citation;
   m_vertical_datum = geodata.m_vertical_datum;

   m_model_pixel_scale_present = geodata.m_model_pixel_scale_present;
   m_model_tie_point_present = geodata.m_model_tie_point_present;
   m_gt_model_type_present = geodata.m_gt_model_type_present;
   m_gt_raster_type_present = geodata.m_gt_raster_type_present;
   m_gt_citation_present = geodata.m_gt_citation_present;
   m_geographic_type_present = geodata.m_geographic_type_present;
   m_geog_citation_present = geodata.m_geog_citation_present;
   m_geog_geodetic_datum_present = geodata.m_geog_geodetic_datum_present;
   m_geog_prime_meridian_present = geodata.m_geog_prime_meridian_present;
   m_geog_prime_meridian_long_present =
      geodata.m_geog_prime_meridian_long_present;
   m_geog_ellipsoid_present = geodata.m_geog_ellipsoid_present;
   m_geog_semi_major_axis_present = geodata.m_geog_semi_major_axis_present;
   m_geog_semi_minor_axis_present = geodata.m_geog_semi_minor_axis_present;
   m_geog_inv_flattening_present = geodata.m_geog_inv_flattening_present;
   m_projected_cs_type_present = geodata.m_projected_cs_type_present;
   m_pcs_citation_present = geodata.m_pcs_citation_present;
   m_projection_present = geodata.m_projection_present;
   m_proj_coord_trans_present = geodata.m_proj_coord_trans_present;
   m_proj_std_parallel_1_present = geodata.m_proj_std_parallel_1_present;
   m_proj_std_parallel_2_present = geodata.m_proj_std_parallel_2_present;
   m_proj_nat_origin_long_present = geodata.m_proj_nat_origin_long_present;
   m_proj_nat_origin_lat_present = geodata.m_proj_nat_origin_lat_present;
   m_proj_false_easting_present = geodata.m_proj_false_easting_present;
   m_proj_false_northing_present = geodata.m_proj_false_northing_present;
   m_proj_false_origin_long_present = geodata.m_proj_false_origin_long_present;
   m_proj_false_origin_lat_present = geodata.m_proj_false_origin_lat_present;
   m_proj_false_origin_easting_present =
      geodata.m_proj_false_origin_easting_present;
   m_proj_false_origin_northing_present =
      geodata.m_proj_false_origin_northing_present;
   m_proj_center_long_present = geodata.m_proj_center_long_present;
   m_proj_center_lat_present = geodata.m_proj_center_lat_present;
   m_proj_center_easting_present = geodata.m_proj_center_easting_present;
   m_proj_center_northing_present = geodata.m_proj_center_northing_present;
   m_proj_scale_at_nat_origin_present =
      geodata.m_proj_scale_at_nat_origin_present;
   m_proj_scale_at_center_present = geodata.m_proj_scale_at_center_present;
   m_proj_azimuth_angle_present = geodata.m_proj_azimuth_angle_present;
   m_proj_straight_vert_pole_long_present =
      geodata.m_proj_straight_vert_pole_long_present;
   m_vertical_cs_type_present = geodata.m_vertical_cs_type_present;
   m_vertical_citation_present = geodata.m_vertical_citation_present;
   m_vertical_datum_present = geodata.m_vertical_datum_present;

   return;

FAIL:
   clear( );
   return;
}

// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************


// CTiffTag class:

gtff::CTiffTag::CTiffTag( )
{
   // constructor

   // set all array pointers to NULL
   m_byte_values = NULL;
   m_ascii_values = NULL;
   m_short_values = NULL;
   m_long_values = NULL;
   m_rational_numerator_values = NULL;
   m_rational_denominator_values = NULL;
   m_sbyte_values = NULL;
   m_undefined_values = NULL;
   m_sshort_values = NULL;
   m_slong_values = NULL;
   m_srational_numerator_values = NULL;
   m_srational_denominator_values = NULL;
   m_float_values = NULL;
   m_double_values = NULL;

   // clear to uninitialized state
   clear( );
}

// *****************************************************************
// *****************************************************************

gtff::CTiffTag::~CTiffTag( )
{
   // destructor

   // clear to uninitialized state
   clear( );
}

// *****************************************************************
// *****************************************************************

void gtff::CTiffTag::clear( )
{
   // clear to uninitialized state

   m_defined = FALSE;
   m_tag_id = 0;
   m_type = 0;
   m_count = 0;
   m_value_offset = 0;
   m_tag_name = "";
   m_type_name = "";
   m_value_name = "";

   // deallocate memory
   if( m_byte_values != NULL )
   {
      delete [] m_byte_values;
      m_byte_values = NULL;
   }

   if( m_ascii_values != NULL )
   {
      delete [] m_ascii_values;
      m_ascii_values = NULL;
   }

   if( m_short_values != NULL )
   {
      delete [] m_short_values;
      m_short_values = NULL;
   }

   if( m_long_values != NULL )
   {
      delete [] m_long_values;
      m_long_values = NULL;
   }

   if( m_rational_numerator_values != NULL )
   {
      delete [] m_rational_numerator_values;
      m_rational_numerator_values = NULL;
   }

   if( m_rational_denominator_values != NULL )
   {
      delete [] m_rational_denominator_values;
      m_rational_denominator_values;
   }

   if( m_sbyte_values != NULL )
   {
      delete [] m_sbyte_values;
      m_sbyte_values = NULL;
   }

   if( m_undefined_values != NULL )
   {
      delete [] m_undefined_values;
      m_undefined_values = NULL;
   }

   if( m_sshort_values != NULL )
   {
      delete [] m_sshort_values;
      m_sshort_values = NULL;
   }

   if( m_slong_values != NULL )
   {
      delete [] m_slong_values;
      m_slong_values = NULL;
   }

   if( m_srational_numerator_values != NULL )
   {
      delete [] m_srational_numerator_values;
      m_srational_numerator_values = NULL;
   }

   if( m_srational_denominator_values != NULL )
   {
      delete [] m_srational_denominator_values;
      m_srational_denominator_values = NULL;
   }

   if( m_float_values != NULL )
   {
      delete [] m_float_values;
      m_float_values = NULL;
   }

   if( m_double_values != NULL )
   {
      delete [] m_double_values;
      m_double_values = NULL;
   }
}

// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************


//345678901234567890123456789012345678901234567890123456789012345678901234567890
// CGeoKey class:

gtff::CGeoKey::CGeoKey( )
{
   // constructor

   // set all array pointers to NULL
   m_ascii_values = NULL;
   m_short_values = NULL;
   m_double_values = NULL;

   // clear to uninitialized state
   clear( );
}

// *****************************************************************
// *****************************************************************

gtff::CGeoKey::~CGeoKey( )
{
   // destructor

   // clear to uninitialized state
   clear( );
}

// *****************************************************************
// *****************************************************************

void gtff::CGeoKey::clear( )
{
   // clear to uninitialized state

   m_defined = FALSE;
   m_geokey_id = 0;
   m_tiff_tag_location = 0;
   m_type = 0;
   m_count = 0;
   m_value_offset = 0;
   m_geokey_name = "";
   m_type_name = "";
   m_value_name = "";

   // deallocate memory
   if( m_ascii_values != NULL )
   {
      delete [] m_ascii_values;
      m_ascii_values = NULL;
   }

   if( m_short_values != NULL )
   {
      delete [] m_short_values;
      m_short_values = NULL;
   }

   if( m_double_values != NULL )
   {
      delete [] m_double_values;
      m_double_values = NULL;
   }
}

// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************



// median cut function - not a class member
int gtff::median_cut( unsigned int *histogram, int num_colors, unsigned char *red,
                unsigned char *green, unsigned char *blue,
                unsigned char *histogram_indices )
{
   // this function implements the median cut method of color quantization
   // the histogram argument contains the cumulative pixel counts for the 32768
   // colors in the reduced 32k color space. the num_colors argument specifies
   // the number of colors after quantization (must be 1 to 256). the red,
   // green and blue arrays should be dimensioned [num_colors] and will be
   // loaded with the quantized colors. the histogram_indices argument should
   // be dimensioned [32768] and will contain the indices for the quantized
   // colors for each of the colors in the histogram
   // returns SUCCESS or FAILURE

   int i, color_index, iparent, ichild, num_parents, num_children, generation;
   int num_colors_used;
   unsigned int red_counts[256], green_counts[256], blue_counts[256];
   unsigned int count, half_count;
   rgb_box parent_boxes[128], child_boxes[256];
   unsigned char histogram_red[32768], histogram_green[32768];
   unsigned char histogram_blue[32768];
   unsigned char min_red, max_red, min_green, max_green, min_blue, max_blue;
   unsigned char mid_red, mid_green, mid_blue;
   unsigned char red_range, green_range, blue_range;
   unsigned char median_red, median_green, median_blue;

   // check for invalid number of quantized colors
   if( num_colors < 1 || num_colors > 256 ) return FAILURE;

   // initialize histogram colors and find number of colors used
   num_colors_used = 0;
   for( i = 0; i < 32768; i++ )
   {
      if( histogram[i] != 0 ) num_colors_used++;
      histogram_red[i] = i >> 10;
      histogram_green[i] = (i >> 5) & 31;
      histogram_blue[i] = i & 31;
   }

   // if the number of colors used is <= the palette size, just copy them
   if( num_colors_used <= num_colors )
   {
      color_index = 0;
      for( i = 0; i < 32768; i++ )
      {
         if( histogram[i] == 0 )
			 continue;
         // convert from 31 to 255 range
         red[color_index] = (unsigned char)( (int)histogram_red[i] *
                            255 / 31 );
         green[color_index] = (unsigned char)( (int)histogram_green[i] *
                              255 / 31 );
         blue[color_index] = (unsigned char)( (int)histogram_blue[i] *
                             255 / 31 );
         histogram_indices[i] = color_index;
         color_index++;
      }
      return SUCCESS;
   }

   // create first parent box containing entire color space and all pixels
   num_parents = 1;
   parent_boxes[0].num_colors = 0;
   parent_boxes[0].small_enough = FALSE;
   parent_boxes[0].min_red = 0;
   parent_boxes[0].max_red = 31;
   parent_boxes[0].min_green = 0;
   parent_boxes[0].max_green = 31;
   parent_boxes[0].min_blue = 0;
   parent_boxes[0].max_blue = 31;

   generation = 1;

CREATE_CHILDREN:
   num_children = 0;
   for( iparent = 0; iparent < num_parents; iparent++ )
   {
      // check for limit on number of colors
      if( num_children + num_parents - iparent + 1 > num_colors )
      {
         // copy remaining parents to children
         for( i = iparent; i < num_parents; i++ )
         {
            child_boxes[num_children] = parent_boxes[i];
            num_children++;
         }
         goto QUANTIZE;
      }

      // check for small enough without subdividing
      if( parent_boxes[iparent].small_enough )
      {
         child_boxes[num_children] = parent_boxes[iparent];
         num_children++;
         continue;
      }

      // find max/min red, green, and blue for colors in box
      min_red = 31;
      max_red = 0;
      min_green = 31;
      max_green = 0;
      min_blue = 31;
      max_blue = 0;
      for( i = 0; i < 256; i++ )
         red_counts[i] = green_counts[i] = blue_counts[i] = 0;
      for( i = 0; i < 32768; i++ )
      {
         if( histogram[i] == 0 )
			 continue;
         if(
            histogram_red[i] >= parent_boxes[iparent].min_red &&
            histogram_red[i] <= parent_boxes[iparent].max_red &&
            histogram_green[i] >= parent_boxes[iparent].min_green &&
            histogram_green[i] <= parent_boxes[iparent].max_green &&
            histogram_blue[i] >= parent_boxes[iparent].min_blue &&
            histogram_blue[i] <= parent_boxes[iparent].max_blue )
         {
            parent_boxes[iparent].num_colors++;
            if( histogram_red[i] < min_red ) min_red = histogram_red[i];
            if( histogram_red[i] > max_red ) max_red = histogram_red[i];
            if( histogram_green[i] < min_green ) min_green = histogram_green[i];
            if( histogram_green[i] > max_green ) max_green = histogram_green[i];
            if( histogram_blue[i] < min_blue ) min_blue = histogram_blue[i];
            if( histogram_blue[i] > max_blue ) max_blue = histogram_blue[i];
            red_counts[histogram_red[i]]++;
            green_counts[histogram_green[i]]++;
            blue_counts[histogram_blue[i]]++;
         }
      }
      parent_boxes[iparent].min_red = min_red;
      parent_boxes[iparent].max_red = max_red;
      parent_boxes[iparent].min_green = min_green;
      parent_boxes[iparent].max_green = max_green;
      parent_boxes[iparent].min_blue = min_blue;
      parent_boxes[iparent].max_blue = max_blue;

      // check for no colors in box -> no children
      if( parent_boxes[iparent].num_colors == 0 )
		  continue;

      red_range = max_red - min_red;
      green_range = max_green - min_green;
      blue_range = max_blue - min_blue;

      // check for too small to bother subdividing
      if( red_range < 2 && green_range < 2 && blue_range < 2 )
      {
         child_boxes[num_children] = parent_boxes[iparent];
         num_children++;
         continue;
      }

      // create two children

      // split in red direction
      if( red_range >= green_range && red_range >= blue_range )
      {
         // find median red value
         count = 0;
         for( i = 0; i < 256; i++ )
            count += red_counts[i];
         half_count = count / 2;
         count = 0;
         for( i = 0; i < 256; i++ )
         {
            count += red_counts[i];
            if( count >= half_count )
            {
               median_red = i;
               break;
            }
         }
         mid_red = median_red;
//         mid_red = (unsigned char)( ( (int)min_red + (int)max_red ) / 2 );
         if( mid_red == max_red ) mid_red--;
         // first child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = min_red;
         child_boxes[num_children].max_red = mid_red;
         child_boxes[num_children].min_green = min_green;
         child_boxes[num_children].max_green = max_green;
         child_boxes[num_children].min_blue = min_blue;
         child_boxes[num_children].max_blue = max_blue;
         red_range = mid_red - min_red;
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         // second child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = mid_red+1;
         child_boxes[num_children].max_red = max_red;
         child_boxes[num_children].min_green = min_green;
         child_boxes[num_children].max_green = max_green;
         child_boxes[num_children].min_blue = min_blue;
         child_boxes[num_children].max_blue = max_blue;
         red_range = max_red - mid_red - 1;
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         continue;
      }

      // split in green direction
      if( green_range >= red_range && green_range >= blue_range )
      {
         // find median green value
         count = 0;
         for( i = 0; i < 256; i++ )
            count += green_counts[i];
         half_count = count / 2;
         count = 0;
         for( i = 0; i < 256; i++ )
         {
            count += green_counts[i];
            if( count >= half_count )
            {
               median_green = i;
               break;
            }
         }
         mid_green = median_green;
//         mid_green = (unsigned char)( ( (int)min_green + (int)max_green ) /
//             2 );
         if( mid_green == max_green ) mid_green--;
         // first child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = min_red;
         child_boxes[num_children].max_red = max_red;
         child_boxes[num_children].min_green = min_green;
         child_boxes[num_children].max_green = mid_green;
         child_boxes[num_children].min_blue = min_blue;
         child_boxes[num_children].max_blue = max_blue;
         green_range = mid_green - min_green;
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         // second child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = min_red;
         child_boxes[num_children].max_red = max_red;
         child_boxes[num_children].min_green = mid_green+1;
         child_boxes[num_children].max_green = max_green;
         child_boxes[num_children].min_blue = min_blue;
         child_boxes[num_children].max_blue = max_blue;
         green_range = max_green - mid_green - 1;
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         continue;
      }

      // split in blue direction
      if( blue_range >= red_range && blue_range >= green_range )
      {
         // find median blue value
         count = 0;
         for( i = 0; i < 256; i++ )
            count += blue_counts[i];
         half_count = count / 2;
         count = 0;
         for( i = 0; i < 256; i++ )
         {
            count += blue_counts[i];
            if( count >= half_count )
            {
               median_blue = i;
               break;
            }
         }
         mid_blue = median_blue;
//         mid_blue = (unsigned char)( ( (int)min_blue + (int)max_blue ) / 2 );
         if( mid_blue == max_blue ) mid_blue--;
         // first child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = min_red;
         child_boxes[num_children].max_red = max_red;
         child_boxes[num_children].min_green = min_green;
         child_boxes[num_children].max_green = max_green;
         child_boxes[num_children].min_blue = min_blue;
         child_boxes[num_children].max_blue = mid_blue;
         blue_range = mid_blue - min_blue;
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         // second child
         child_boxes[num_children].num_colors = 0;
         child_boxes[num_children].min_red = min_red;
         child_boxes[num_children].max_red = max_red;
         child_boxes[num_children].min_green = min_green;
         child_boxes[num_children].max_green = max_green;
         child_boxes[num_children].min_blue = mid_blue+1;
         child_boxes[num_children].max_blue = max_blue;
         blue_range = max_blue - mid_blue - 1;
         if( red_range < 2 && green_range < 2 && blue_range < 2 )
            child_boxes[num_children].small_enough = TRUE;
         else
            child_boxes[num_children].small_enough = FALSE;
         num_children++;
         continue;
      }
   }

   generation++;
   if( generation == 9 )
      goto QUANTIZE;
   else
   {
      // make parents of children
      for( i = 0; i < num_children; i++ )
         parent_boxes[i] = child_boxes[i];
      num_parents = num_children;
      goto CREATE_CHILDREN;
   }

QUANTIZE:
   // find midpoint of each child box as the color map color
   for( i = 0; i < num_children; i++ )
   {
      red[i] = (unsigned char)( ( (int)child_boxes[i].min_red +
                      (int)child_boxes[i].max_red ) / 2 );
      green[i] = (unsigned char)( ( (int)child_boxes[i].min_green +
                        (int)child_boxes[i].max_green ) / 2 );
      blue[i] = (unsigned char)( ( (int)child_boxes[i].min_blue +
                       (int)child_boxes[i].max_blue ) / 2 );
      // convert from 31 to 255 range
      red[i] = (unsigned char)( (int)red[i] * 255 / 31 );
      green[i] = (unsigned char)( (int)green[i] * 255 / 31 );
      blue[i] = (unsigned char)( (int)blue[i] * 255 / 31 );
   }

   // find the child box for each color
   for( i = 0; i < 32768; i++ )
   {
      histogram_indices[i] = 0;
      if( histogram[i] == 0 )
		  continue;
      for( ichild = 0; ichild < num_children; ichild++ )
      {
         if(
            histogram_red[i] >= child_boxes[ichild].min_red &&
            histogram_red[i] <= child_boxes[ichild].max_red &&
            histogram_green[i] >= child_boxes[ichild].min_green &&
            histogram_green[i] <= child_boxes[ichild].max_green &&
            histogram_blue[i] >= child_boxes[ichild].min_blue &&
            histogram_blue[i] <= child_boxes[ichild].max_blue )
         {
            histogram_indices[i] = ichild;
            break;
         }
      }
   }

   return SUCCESS;
}

// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************

//345678901234567890123456789012345678901234567890123456789012345678901234567890
// CGeoTiff class:

CGeoTiffFrameFile::CGeoTiffFrameFile( )
{
   // constructor

   // initialize file pointer to NULL
   m_file_ptr = NULL;

   // initialize tag array pointer to NULL
   m_tags = NULL;

   // initialize geokey array pointer to NULL
   m_geokeys = NULL;

   // initialize color map array pointers to NULL
   m_red_color_map = NULL;
   m_green_color_map = NULL;
   m_blue_color_map = NULL;

   // initialize tile arrays
   m_tile_red_array = NULL;
   m_tile_green_array = NULL;
   m_tile_blue_array = NULL;
   m_tile_color_indices = NULL;

   // init jpeg table len
   m_jpeg_table_len = 0;

   // clear
   clear( );

#ifdef _WIN32
   m_imagelib = NULL;
#endif
}

// *****************************************************************
// *****************************************************************

CGeoTiffFrameFile::~CGeoTiffFrameFile( )
{
   // destructor

   // clear
   clear( );
}

// *****************************************************************
// *****************************************************************

void CGeoTiffFrameFile::clear( )
{
   // set loaded flag to false
   m_file_loaded = FALSE;

   // clear file name
   m_tiff_file_name = "";

   // clear error flag
   m_error = FALSE;

   // clear error description
   m_cumulative_error_description = "";
   m_new_error_description = "";

   // byte order
   m_byte_order = GEOTIFF_BYTE_ORDER_NOT_DEFINED;

   // number of directories
   m_num_directories = 0;

   // initialize first directory offset to zero
   m_directory_offset = 0;

   // if file is open, close and set file pointer to null
   if( m_file_ptr != NULL )
   {
      fclose( m_file_ptr );
      m_file_ptr = NULL;
   }

   // number of tags
   m_num_tags = 0;

   // delete any allocated tags
   if( m_tags != NULL )
   {
      delete [] m_tags;
      m_tags = NULL;
   }

   // geokey directory info
   m_geokey_directory_version = 0;
   m_geokey_directory_revision = 0;
   m_geokey_directory_minor_revision = 0;
   m_num_geokeys = 0;

   // delete any allocated geokeys
   if( m_geokeys != NULL )
   {
      delete [] m_geokeys;
      m_geokeys = NULL;
   }

   // image information
   m_image_width = 0;
   m_image_length = 0;
   m_num_pixels = 0;
   m_compression_scheme = 0;
   m_predictor = 1;
   m_photometric_interpretation = 0;
   m_rows_per_strip = 0;
   m_num_strips = 0;
   m_x_resolution = 0.0;
   m_y_resolution = 0.0;
   m_resolution_unit = 0;
   m_planar_configuration = 0;
   m_samples_per_pixel = 0;
   m_bits_per_sample = 0;
   m_fill_order = 0;
   m_orientation = 0;
   m_image_type_supported = FALSE;
   m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
   m_image_type_description = "Image type is unknown.";

   // color map
   m_color_map_present = FALSE;
   m_num_color_map_values = 0;
   if( m_red_color_map != NULL )
   {
      delete [] m_red_color_map;
      m_red_color_map = NULL;
   }
   if( m_green_color_map != NULL )
   {
      delete [] m_green_color_map;
      m_green_color_map = NULL;
   }
   if( m_blue_color_map != NULL )
   {
      delete [] m_blue_color_map;
      m_blue_color_map = NULL;
   }

   // max image strip size
   m_max_strip_byte_count = 0;

   // histogram
   memset(m_histogram, 0, sizeof(unsigned int)* 32768);

   // geodata
   m_geodata_present = FALSE;
   m_geodata_supported = FALSE;
   m_geodata_error_code = GEOTIFF_GEODATA_NOT_PRESENT;
   m_geodata_error_message = "";

   // geodata object
   m_geodata.clear( );

   // tiepoint transform
   m_tiepoint_transform.clear( );

   // tile data
   m_image_is_tiled = FALSE;
   m_tile_width = 0;
   m_tile_length = 0;
   m_num_tile_pixels = 0;
   m_num_tiles_across = 0;
   m_num_tiles_down = 0;
   m_num_tiles = 0;
   if( m_tile_red_array != NULL )
   {
      delete [] m_tile_red_array;
      m_tile_red_array = NULL;
   }
   if( m_tile_green_array != NULL )
   {
      delete [] m_tile_green_array;
      m_tile_green_array = NULL;
   }
   if( m_tile_blue_array != NULL )
   {
      delete [] m_tile_blue_array;
      m_tile_blue_array = NULL;
   }
   if( m_tile_color_indices != NULL )
   {
      delete [] m_tile_color_indices;
      m_tile_color_indices = NULL;
   }
}
// end of clear

// *****************************************************************
// *****************************************************************

BOOL CGeoTiffFrameFile::is_tiled()
{
	return m_image_is_tiled;
}


// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::load( std::string tiff_file_name, BOOL &image_type_supported,
                            int &image_type, std::string &image_type_description,
                    int &image_width, int &image_length,
                    BOOL &geodata_present, BOOL &geodata_supported,
                    std::string &error_message )
{
   // this function loads the specified tiff file, obtaining information
   // about the file
   // error message returned in error_message argument
   // returns SUCCESS or FAILURE

   char byte_order_string[3];   // first 2 characters determine byte order
   short short_value;

   error_message = "";
   image_type_supported = FALSE;
   image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
   image_type_description = "";
   image_width = 0;
   image_length = 0;
   m_image_is_tiled = FALSE;
   m_tile_width = 0;
   m_tile_length = 0;
   m_num_tile_pixels = 0;
   m_num_tiles_across = 0;
   m_num_tiles_down = 0;
   m_num_tiles = 0;

   geodata_present = FALSE;
   geodata_supported = FALSE;

   // clear first
   clear( );

   // attempt to open file
   ATLTRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   m_tiff_file_name = tiff_file_name;
   m_file_ptr = NULL;
   fopen_s( &m_file_ptr, m_tiff_file_name.c_str(), "rb" );
   if( m_file_ptr == NULL )
   {
      THROW_ERROR_MSG(HRESULT_FROM_WIN32(GetLastError()), "load", "Error opening GeoTIFF frame")
   }

   // read first two bytes, which determine byte order
   byte_order_string[2] = NULL;
   if( fread( byte_order_string, 1, 2, m_file_ptr ) != 2 )
   {
      m_error = TRUE;
      m_cumulative_error_description =
         "Error reading byte order string in file header.";
      error_message = m_cumulative_error_description;
      return FAILURE;
   }

   // test for byte order
   if( strcmp( byte_order_string, "II" ) == 0 )
   {
      // byte order is Intel little-endian
      m_byte_order = GEOTIFF_LITTLE_ENDIAN;
   }
   else
      if( strcmp( byte_order_string, "MM" ) == 0 )
      {
         // byte order is Motorola big-endian
         m_byte_order = GEOTIFF_BIG_ENDIAN;
      }
   if( m_byte_order == GEOTIFF_BYTE_ORDER_NOT_DEFINED )
   {
      m_error = TRUE;
      m_cumulative_error_description = "Error in byte order string: ";
      m_cumulative_error_description += byte_order_string;
      error_message = m_cumulative_error_description;
      return FAILURE;
   }

   // check for short int with value 42 in bytes 2-3
   if( read_signed_short( short_value ) != SUCCESS )
   {
      m_error = TRUE;
      m_cumulative_error_description =
         "Error reading bytes 2-3 of file header.";
      error_message = m_cumulative_error_description;
      return FAILURE;
   }
   if( short_value != 42 )
   {
      m_error = TRUE;
      m_cumulative_error_description =
         "Error in value of bytes 2-3 of file header.";
      error_message = m_cumulative_error_description;
      return FAILURE;
   }

   // read offset of first image file directory from bytes 4-7
   if( read_unsigned_int( m_directory_offset ) != SUCCESS )
   {
      m_error = TRUE;
      m_cumulative_error_description =
         "Error reading first directory offset in file header.";
      error_message = m_cumulative_error_description;
      return FAILURE;
   }
   m_num_directories = 1;

   // read tag directory and tags
   if( read_directory( ) != SUCCESS )
   {
      m_error = TRUE;
      m_cumulative_error_description += "Error in Tag Data: ";
      m_cumulative_error_description += m_new_error_description;
      m_cumulative_error_description += " / ";
   }

   // inspect tags for image information
   if( inspect_tags( ) != SUCCESS )
   {
      m_error = TRUE;
      m_cumulative_error_description += "Error in Image Data: ";
      m_cumulative_error_description += m_new_error_description;
      m_cumulative_error_description += " / ";
   }

   // read geokey directory and geokeys
   if( read_geokey_directory( ) != SUCCESS )
   {
      m_error = TRUE;
      m_cumulative_error_description += "Error in GeoKey Data: ";
      m_cumulative_error_description += m_new_error_description;
      m_cumulative_error_description += " / ";
   }

   // file has been loaded
   m_file_loaded = TRUE;

   // set error message argument
   error_message = m_cumulative_error_description;

   // check for geodata
   if( !m_error )
      check_geodata( );

   // set image type arguments
   image_type_supported = m_image_type_supported;
   image_type = m_image_type;
   image_type_description = m_image_type_description;
   image_width = m_image_width;
   image_length = m_image_length;
   geodata_present = m_geodata_present;
   geodata_supported = m_geodata_supported;

   // return SUCCESS or FAILURE depending on whether errors ocurred
   if( m_error )
      return FAILURE;
   else
      return SUCCESS;
}
// end of load

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_geodata( CGeoData &geodata, int &error_code,
                           std::string &error_message )
{
   // this function returns the geodata in the geodata object
   // all quantities are returned in length units of meters and
   // angle units of degrees by adjusting file values if necessary
   // returns SUCCESS or FAILURE and descriptive error code and message
   // returns FAILURE if geodata is not present or is not supported

   // return failure if no file loaded or there was an error loading the
   // file
   if( !m_file_loaded || m_error )
   {
      m_geodata_error_message = "File is not loaded.";
      return FAILURE;
   }

   geodata = m_geodata;
   error_code = m_geodata_error_code;
   error_message = m_geodata_error_message;

   // return failure if geodata is not present
   if( m_geodata_present )
      return SUCCESS;
   else
      return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_image_description( std::string &image_description )
{
   // returns the image description if present
   // returns SUCCESS or FAILURE if image description tag is not present

   int tag_index;

   // return failure if no file loaded or there was an error loading the
   // file
   if( !m_file_loaded || m_error )
      return FAILURE;

   if( find_tag( GEOTIFF_IMAGE_DESCRIPTION_TAG, tag_index ) != SUCCESS )
      goto FAIL;

   if (m_tags[tag_index].m_ascii_values != NULL)
	 image_description = m_tags[tag_index].m_ascii_values;

   return SUCCESS;

FAIL:
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_image_width( int &image_width )
{
   // returns the image width in the argument
   // returns SUCCESS or FAILURE

   // return failure if no file loaded or there was an error loading the
   // file
   if( !m_file_loaded || m_error )
      return FAILURE;

   image_width = m_image_width;
   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_image_length( int &image_length )
{
   // returns the image length in the argument
   // returns SUCCESS or FAILURE

   // return failure if no file loaded or there was an error loading the
   // file
   if( !m_file_loaded || m_error )
      return FAILURE;

   image_length = m_image_length;
   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_image_type( int &image_type )
{
   // returns the image type in the argument
   // returns SUCCESS or FAILURE

   // return failure if no file loaded or there was an error loading the
   // file
   if( !m_file_loaded || m_error )
      return FAILURE;

   image_type = m_image_type;
   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_scale( int &horizontal_scale, int &vertical_scale )
{
   // returns the horizontal and vertical scales of the image if available
   // such as 24000 for a 1:24000 image
   // returns SUCCESS or FAILURE

   int tag_index;
   unsigned short resolution_unit;
   double horizontal_meters_per_pixel, vertical_meters_per_pixel;
   double horizontal_resolution, vertical_resolution;
   double h_scale, v_scale;

   // check for model pixel scale present
   if( !m_geodata.m_model_pixel_scale_present )
      goto FAIL;
   horizontal_meters_per_pixel = fabs( m_geodata.m_model_pixel_scale_x );
   vertical_meters_per_pixel = fabs( m_geodata.m_model_pixel_scale_y );

   // get resolution (scanning) unit
   if( find_tag( GEOTIFF_RESOLUTION_UNIT_TAG, tag_index ) != SUCCESS )
      goto FAIL;
   resolution_unit = m_tags[tag_index].m_short_values[0];

   // get horizontal scanning resolution
   if( find_tag( GEOTIFF_X_RESOLUTION_TAG, tag_index ) != SUCCESS )
      goto FAIL;
   horizontal_resolution = m_tags[tag_index].m_double_values[0];

   // get vertical scanning resolution
   if( find_tag( GEOTIFF_Y_RESOLUTION_TAG, tag_index ) != SUCCESS )
      goto FAIL;
   vertical_resolution = m_tags[tag_index].m_double_values[0];

   switch( resolution_unit )
   {
      case GEOTIFF_RESOLUTION_UNIT_INCH:
         h_scale = horizontal_resolution * horizontal_meters_per_pixel *
                   (100.0/2.54);
         v_scale = vertical_resolution * vertical_meters_per_pixel *
                   (100.0/2.54);
         break;

      case GEOTIFF_RESOLUTION_UNIT_CENTIMETER:
         h_scale = horizontal_resolution * horizontal_meters_per_pixel *
                   100.0;
         v_scale = vertical_resolution * vertical_meters_per_pixel *
                   100.0;
         break;

      default:
         goto FAIL;
   }

   // round scales to nearest integers
   horizontal_scale = (int)( h_scale + 0.5 );
   vertical_scale  = (int)( v_scale + 0.5 );
   return SUCCESS;

FAIL:
   return FAILURE;
}

// ****************************************************************
// *****************************************************************

void CGeoTiffFrameFile::check_geodata(  )
{
   // this function checks for the presence of geodata and, if present,
   // loads the geodata object
   // all quantities are converted to length units of meters and
   // angle units of degrees by adjusting file values if necessary

   int i, j, index;

   unsigned short geog_linear_units, geog_angular_units, geog_azimuth_units;
   double geog_linear_unit_size, geog_angular_unit_size;
   BOOL geog_linear_units_present, geog_angular_units_present;
   BOOL geog_azimuth_units_present;
   BOOL geog_linear_unit_size_present, geog_angular_unit_size_present;

   unsigned short proj_linear_units;
   double proj_linear_unit_size;
   BOOL proj_linear_units_present, proj_linear_unit_size_present;

   unsigned short vertical_units;
   BOOL vertical_units_present;

   // clear the geodata structure
   m_geodata.clear( );

   // check for geokeys present
   if( m_num_geokeys == 0 )
   {
      m_geodata_present = FALSE;
      m_geodata_supported = FALSE;
      m_geodata_error_code = GEOTIFF_GEODATA_NOT_PRESENT;
      m_geodata_error_message = "Geodata not present";
      return;
   }
   else
      m_geodata_present = TRUE;

   // initialize geographic units as unspecified
   geog_linear_units = 0;
   geog_angular_units = 0;
   geog_azimuth_units = 0;
   geog_linear_unit_size = 0.0;
   geog_angular_unit_size = 0.0;
   geog_linear_units_present = FALSE;
   geog_angular_units_present = FALSE;
   geog_azimuth_units_present = FALSE;
   geog_linear_unit_size_present = FALSE;
   geog_angular_unit_size_present = FALSE;

   // initialize projection units as unspecified
   proj_linear_units = 0;
   proj_linear_unit_size = 0.0;
   proj_linear_units_present = FALSE;
   proj_linear_unit_size_present = FALSE;

   // initialize vertical units as unspecified
   vertical_units = 0;
   vertical_units_present = FALSE;

   // fill in data from tags and geokeys

   // get model pixel scale
   if( find_tag( GEOTIFF_MODEL_PIXEL_SCALE_TAG, index ) == SUCCESS )
   {
      m_geodata.m_model_pixel_scale_x = m_tags[index].m_double_values[0];
      m_geodata.m_model_pixel_scale_y = m_tags[index].m_double_values[1];
      m_geodata.m_model_pixel_scale_z = m_tags[index].m_double_values[2];
      if( m_geodata.m_model_pixel_scale_x == 0.0 )
      {
         m_geodata_error_message = "Model X pixel scale is zero.";
         goto FAIL;
      }
      if( m_geodata.m_model_pixel_scale_y == 0.0 )
      {
         m_geodata_error_message = "Model Y pixel scale is zero.";
         goto FAIL;
      }
      m_geodata.m_model_pixel_scale_present = TRUE;
   }

   // get model tiepoints
   if( find_tag( GEOTIFF_MODEL_TIEPOINT_TAG, index ) != SUCCESS )
   {
      m_geodata_error_message = "Model tiepoint tag is missing.";
      goto FAIL;
   }
   m_geodata.m_num_tie_points = m_tags[index].m_count / 6;
   if( m_geodata.m_num_tie_points > 0 )
   {
      // allocate memory for tiepoints
      m_geodata.m_model_tie_point_hpix = new double[m_geodata.m_num_tie_points];
      if( m_geodata.m_model_tie_point_hpix == NULL )
         goto FAIL;
      m_geodata.m_model_tie_point_vpix = new double[m_geodata.m_num_tie_points];
      if( m_geodata.m_model_tie_point_vpix == NULL )
         goto FAIL;
      m_geodata.m_model_tie_point_zpix = new double[m_geodata.m_num_tie_points];
      if( m_geodata.m_model_tie_point_zpix == NULL )
         goto FAIL;
      m_geodata.m_model_tie_point_x = new double[m_geodata.m_num_tie_points];
      if( m_geodata.m_model_tie_point_x == NULL )
         goto FAIL;
      m_geodata.m_model_tie_point_y = new double[m_geodata.m_num_tie_points];
      if( m_geodata.m_model_tie_point_y == NULL )
         goto FAIL;
      m_geodata.m_model_tie_point_z = new double[m_geodata.m_num_tie_points];
      if( m_geodata.m_model_tie_point_z == NULL )
         goto FAIL;
      // copy the tie point coordinates
      for( i = 0; i < m_geodata.m_num_tie_points; i++ )
      {
         j = 6 * i;
         m_geodata.m_model_tie_point_hpix[i] =
            m_tags[index].m_double_values[j];
         m_geodata.m_model_tie_point_vpix[i] =
            m_tags[index].m_double_values[j+1];
         m_geodata.m_model_tie_point_zpix[i] =
            m_tags[index].m_double_values[j+2];
         m_geodata.m_model_tie_point_x[i] =
            m_tags[index].m_double_values[j+3];
         m_geodata.m_model_tie_point_y[i] =
            m_tags[index].m_double_values[j+4];
         m_geodata.m_model_tie_point_z[i] =
            m_tags[index].m_double_values[j+5];
      }
   }
   if( m_geodata.m_num_tie_points > 0 )
      m_geodata.m_model_tie_point_present = TRUE;

   // loop through all geokeys
   for( index = 0; index < m_num_geokeys; index++ )
   {
      switch( m_geokeys[index].m_geokey_id )
      {
         case GEOTIFF_GT_MODEL_TYPE_GEOKEY:
			 if (m_geokeys[index].m_short_values != NULL)
			 {
				m_geodata.m_gt_model_type = m_geokeys[index].m_short_values[0];
				m_geodata.m_gt_model_type_present = TRUE;
			 }
            break;
         case GEOTIFF_GT_RASTER_TYPE_GEOKEY:
			 if (m_geokeys[index].m_short_values != NULL)
			 {
				m_geodata.m_gt_raster_type = m_geokeys[index].m_short_values[0];
				m_geodata.m_gt_raster_type_present = TRUE;
			 }
            break;
         case GEOTIFF_GT_CITATION_GEOKEY:
			 if (m_geokeys[index].m_ascii_values != NULL)
			 {
				m_geodata.m_gt_citation = m_geokeys[index].m_ascii_values;
				m_geodata.m_gt_citation_present = TRUE;
			 }
            break;

         case GEOTIFF_GEOGRAPHIC_TYPE_GEOKEY:
			 if (m_geokeys[index].m_short_values != NULL)
			 {
				m_geodata.m_geographic_type = m_geokeys[index].m_short_values[0];
				m_geodata.m_geographic_type_present = TRUE;
			 }
            break;
         case GEOTIFF_GEOG_CITATION_GEOKEY:
			 if (m_geokeys[index].m_ascii_values != NULL)
			 {
				m_geodata.m_geog_citation = m_geokeys[index].m_ascii_values;
				m_geodata.m_geog_citation_present = TRUE;
			 }
            break;
         case GEOTIFF_GEOG_GEODETIC_DATUM_GEOKEY:
			 if (m_geokeys[index].m_short_values != NULL)
			 {
				m_geodata.m_geog_geodetic_datum = m_geokeys[index].m_short_values[0];
				m_geodata.m_geog_geodetic_datum_present = TRUE;
			 }
            break;
         case GEOTIFF_GEOG_PRIME_MERIDIAN_GEOKEY:
			 if (m_geokeys[index].m_short_values != NULL)
			 {
				m_geodata.m_geog_prime_meridian = m_geokeys[index].m_short_values[0];
				m_geodata.m_geog_prime_meridian_present = TRUE;
			 }
            break;
         case GEOTIFF_GEOG_PRIME_MERIDIAN_LONG_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_geog_prime_meridian_long = m_geokeys[index].m_double_values[0];
				m_geodata.m_geog_prime_meridian_long_present = TRUE;
			 }
            break;
         case GEOTIFF_GEOG_LINEAR_UNITS_GEOKEY:
			 if (m_geokeys[index].m_short_values != NULL)
			 {
				geog_linear_units = m_geokeys[index].m_short_values[0];
				geog_linear_units_present = TRUE;
			 }
            break;
         case GEOTIFF_GEOG_LINEAR_UNIT_SIZE_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				geog_linear_unit_size = m_geokeys[index].m_double_values[0];
				geog_linear_unit_size_present = TRUE;
			 }
            break;
         case GEOTIFF_GEOG_ANGULAR_UNITS_GEOKEY:
			 if (m_geokeys[index].m_short_values != NULL)
			 {
				geog_angular_units = m_geokeys[index].m_short_values[0];
				geog_angular_units_present = TRUE;
			 }
            break;
         case GEOTIFF_GEOG_ANGULAR_UNIT_SIZE_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				geog_angular_unit_size = m_geokeys[index].m_double_values[0];
				geog_angular_unit_size_present = TRUE;
			 }
            break;
         case GEOTIFF_GEOG_ELLIPSOID_GEOKEY:
			 if (m_geokeys[index].m_short_values != NULL)
			 {
				m_geodata.m_geog_ellipsoid = m_geokeys[index].m_short_values[0];
				m_geodata.m_geog_ellipsoid_present = TRUE;
			 }
            break;
         case GEOTIFF_GEOG_SEMI_MAJOR_AXIS_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_geog_semi_major_axis = m_geokeys[index].m_double_values[0];
				m_geodata.m_geog_semi_major_axis_present = TRUE;
			 }
            break;
         case GEOTIFF_GEOG_SEMI_MINOR_AXIS_GEOKEY:
 			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_geog_semi_minor_axis = m_geokeys[index].m_double_values[0];
				m_geodata.m_geog_semi_minor_axis_present = TRUE;
			 }
            break;
         case GEOTIFF_GEOG_INV_FLATTENING_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_geog_inv_flattening = m_geokeys[index].m_double_values[0];
				m_geodata.m_geog_inv_flattening_present = TRUE;
			 }
            break;
         case GEOTIFF_GEOG_AZIMUTH_UNITS_GEOKEY:
			 if (m_geokeys[index].m_short_values != NULL)
			 {
				geog_azimuth_units = m_geokeys[index].m_short_values[0];
				geog_azimuth_units_present = FALSE;
			 }
            break;

         case GEOTIFF_PROJECTED_CS_TYPE_GEOKEY:
			 if (m_geokeys[index].m_short_values != NULL)
			 {
				m_geodata.m_projected_cs_type = m_geokeys[index].m_short_values[0];
				m_geodata.m_projected_cs_type_present = TRUE;
			 }
            break;
         case GEOTIFF_PCS_CITATION_GEOKEY:
			 if (m_geokeys[index].m_ascii_values != NULL)
			 {
				m_geodata.m_pcs_citation = m_geokeys[index].m_ascii_values;
				m_geodata.m_pcs_citation_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJECTION_GEOKEY:
			 if (m_geokeys[index].m_short_values != NULL)
			 {
				m_geodata.m_projection = m_geokeys[index].m_short_values[0];
				m_geodata.m_projection_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_COORD_TRANS_GEOKEY:
			 if (m_geokeys[index].m_short_values != NULL)
			 {
				m_geodata.m_proj_coord_trans = m_geokeys[index].m_short_values[0];
				m_geodata.m_proj_coord_trans_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_LINEAR_UNITS_GEOKEY:
			 if (m_geokeys[index].m_short_values != NULL)
			 {
				proj_linear_units = m_geokeys[index].m_short_values[0];
				proj_linear_units_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_LINEAR_UNIT_SIZE_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				proj_linear_unit_size = m_geokeys[index].m_double_values[0];
				proj_linear_unit_size_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_STD_PARALLEL_1_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_std_parallel_1 = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_std_parallel_1_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_STD_PARALLEL_2_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_std_parallel_2 = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_std_parallel_2_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_NAT_ORIGIN_LONG_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_nat_origin_long = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_nat_origin_long_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_NAT_ORIGIN_LAT_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_nat_origin_lat = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_nat_origin_lat_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_FALSE_EASTING_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_false_easting = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_false_easting_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_FALSE_NORTHING_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_false_northing = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_false_northing_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_FALSE_ORIGIN_LONG_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_false_origin_long = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_false_origin_long_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_FALSE_ORIGIN_LAT_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_false_origin_lat = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_false_origin_lat_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_FALSE_ORIGIN_EASTING_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_false_origin_easting = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_false_origin_easting_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_FALSE_ORIGIN_NORTHING_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_false_origin_northing = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_false_origin_northing_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_CENTER_LONG_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_center_long = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_center_long_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_CENTER_LAT_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_center_lat = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_center_lat_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_CENTER_EASTING_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_center_easting = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_center_easting_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_CENTER_NORTHING_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_center_northing = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_center_northing_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_SCALE_AT_NAT_ORIGIN_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_scale_at_nat_origin = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_scale_at_nat_origin_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_SCALE_AT_CENTER_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_scale_at_center = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_scale_at_center_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_AZIMUTH_ANGLE_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_azimuth_angle = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_azimuth_angle_present = TRUE;
			 }
            break;
         case GEOTIFF_PROJ_STRAIGHT_VERT_POLE_LONG_GEOKEY:
			 if (m_geokeys[index].m_double_values != NULL)
			 {
				m_geodata.m_proj_straight_vert_pole_long = m_geokeys[index].m_double_values[0];
				m_geodata.m_proj_straight_vert_pole_long_present = TRUE;
			 }
            break;

         case GEOTIFF_VERTICAL_CS_TYPE_GEOKEY:
			 if (m_geokeys[index].m_short_values != NULL)
			 {
				m_geodata.m_vertical_cs_type = m_geokeys[index].m_short_values[0];
				m_geodata.m_vertical_cs_type_present = TRUE;
			 }
            break;
         case GEOTIFF_VERTICAL_CITATION_GEOKEY:
			 if (m_geokeys[index].m_ascii_values != NULL)
			 {
				m_geodata.m_vertical_citation = m_geokeys[index].m_ascii_values;
				m_geodata.m_vertical_citation_present = TRUE;
			 }
            break;
         case GEOTIFF_VERTICAL_DATUM_GEOKEY:
			 if (m_geokeys[index].m_short_values != NULL)
			 {
				m_geodata.m_vertical_datum = m_geokeys[index].m_short_values[0];
				m_geodata.m_vertical_datum_present = TRUE;
			 }
            break;
         case GEOTIFF_VERTICAL_UNITS_GEOKEY:
			 if (m_geokeys[index].m_short_values != NULL)
			 {
				vertical_units = m_geokeys[index].m_short_values[0];
				vertical_units_present = TRUE;
			 }
            break;
         case GEOTIFF_UNDEFINED:
            break;
         case GEOTIFF_USER_DEFINED:
            break;
         default:
            break;
      }
   }

   m_geog_linear_units = geog_linear_units;

   // check that raster type is present recognized
   if( !m_geodata.m_gt_raster_type_present )
   {
		// not specified, use the default
		m_geodata.m_gt_raster_type = GEOTIFF_RASTER_PIXEL_IS_AREA;
		m_geodata.m_gt_raster_type_present = TRUE;
   }
   else if ( m_geodata.m_gt_raster_type != GEOTIFF_RASTER_PIXEL_IS_AREA &&
			m_geodata.m_gt_raster_type != GEOTIFF_RASTER_PIXEL_IS_POINT )
   {
		// invalid raster type, use the default
		m_geodata.m_gt_raster_type = GEOTIFF_RASTER_PIXEL_IS_AREA;
		m_geodata.m_gt_raster_type_present = TRUE;
   }

   // model type must be present
   if( !m_geodata.m_gt_model_type_present )
   {
      m_geodata_error_message = "Model type geokey is missing.";
      goto FAIL;
   }

   // convert geographic linear units to meters
   if( geog_linear_units_present )
   {
      // check for supported units
      switch( geog_linear_units )
      {
         case GEOTIFF_USER_DEFINED:
            if( !geog_linear_unit_size_present )
            {
               m_geodata_error_message =
                  "User-defined geographic linear unit size not present.";
               goto FAIL;
            }
         case GEOTIFF_LINEAR_METER:
         case GEOTIFF_LINEAR_FOOT:
         case GEOTIFF_LINEAR_FOOT_US_SURVEY:
         case GEOTIFF_LINEAR_FOOT_MODIFIED_AMERICAN:
         case GEOTIFF_LINEAR_FOOT_CLARKE:
         case GEOTIFF_LINEAR_FOOT_INDIAN:
         case GEOTIFF_LINEAR_LINK:
         case GEOTIFF_LINEAR_LINK_BENOIT:
         case GEOTIFF_LINEAR_LINK_SEARS:
         case GEOTIFF_LINEAR_CHAIN_BENOIT:
         case GEOTIFF_LINEAR_CHAIN_SEARS:
         case GEOTIFF_LINEAR_YARD_SEARS:
         case GEOTIFF_LINEAR_YARD_INDIAN:
         case GEOTIFF_LINEAR_FATHOM:
         case GEOTIFF_LINEAR_MILE_INTERNATIONAL_NAUTICAL:
            convert_linear_units( geog_linear_units, geog_linear_unit_size,
                                  m_geodata.m_geog_semi_major_axis );
            convert_linear_units( geog_linear_units, geog_linear_unit_size,
                                  m_geodata.m_geog_semi_minor_axis );
            break;
         case GEOTIFF_UNDEFINED:
            m_geodata_error_message = "Geographic linear unit is undefined.";
            goto FAIL;
         default:
            m_geodata_error_message = "Geographic linear unit is unknown.";
            goto FAIL;
      }
   }

   // convert angular units to degrees
   if( geog_angular_units_present )
   {
      // check for supported units
      switch( geog_angular_units )
      {
         case GEOTIFF_USER_DEFINED:
            if( !geog_angular_unit_size_present )
            {
               m_geodata_error_message =
                  "User-defined geographic angular unit size not present.";
               goto FAIL;
            }
         case GEOTIFF_ANGULAR_RADIAN:
         case GEOTIFF_ANGULAR_DEGREE:
         case GEOTIFF_ANGULAR_ARC_MINUTE:
         case GEOTIFF_ANGULAR_ARC_SECOND:
         case GEOTIFF_ANGULAR_GRAD:
         case GEOTIFF_ANGULAR_GON:
            convert_angular_units( geog_angular_units, geog_angular_unit_size,
                                   m_geodata.m_geog_prime_meridian_long );
            convert_angular_units( geog_angular_units, geog_angular_unit_size,
                                   m_geodata.m_proj_std_parallel_1 );
            convert_angular_units( geog_angular_units, geog_angular_unit_size,
                                   m_geodata.m_proj_std_parallel_2 );
            convert_angular_units( geog_angular_units, geog_angular_unit_size,
                                   m_geodata.m_proj_nat_origin_long );
            convert_angular_units( geog_angular_units, geog_angular_unit_size,
                                   m_geodata.m_proj_nat_origin_lat );
            convert_angular_units( geog_angular_units, geog_angular_unit_size,
                                   m_geodata.m_proj_false_origin_long );
            convert_angular_units( geog_angular_units, geog_angular_unit_size,
                                   m_geodata.m_proj_false_origin_lat );
            convert_angular_units( geog_angular_units, geog_angular_unit_size,
                                   m_geodata.m_proj_center_long );
            convert_angular_units( geog_angular_units, geog_angular_unit_size,
                                   m_geodata.m_proj_center_lat );
            convert_angular_units( geog_angular_units, geog_angular_unit_size,
                                   m_geodata.m_proj_straight_vert_pole_long );
            break;

         case GEOTIFF_ANGULAR_DMS:
            m_geodata_error_message =
               "Geographic angular units not supported: DMS.";
            goto FAIL;
         case GEOTIFF_ANGULAR_DMS_HEMISPHERE:
            m_geodata_error_message =
               "Geographic angular units not supported: DMS Hemisphere.";
            goto FAIL;
         case GEOTIFF_UNDEFINED:
            m_geodata_error_message = "Geographic angular unit is undefined.";
            goto FAIL;
         default:
            m_geodata_error_message = "Geographic angular unit is unknown.";
            goto FAIL;
      }
   }

   // convert projection linear units to meters
   if( proj_linear_units_present )
   {
      // check for supported units
      switch( proj_linear_units )
      {
         case GEOTIFF_USER_DEFINED:
            if( !proj_linear_unit_size_present )
            {
               m_geodata_error_message =
                  "User-defined projection linear unit size not present.";
               goto FAIL;
            }
         case GEOTIFF_LINEAR_METER:
         case GEOTIFF_LINEAR_FOOT:
         case GEOTIFF_LINEAR_FOOT_US_SURVEY:
         case GEOTIFF_LINEAR_FOOT_MODIFIED_AMERICAN:
         case GEOTIFF_LINEAR_FOOT_CLARKE:
         case GEOTIFF_LINEAR_FOOT_INDIAN:
         case GEOTIFF_LINEAR_LINK:
         case GEOTIFF_LINEAR_LINK_BENOIT:
         case GEOTIFF_LINEAR_LINK_SEARS:
         case GEOTIFF_LINEAR_CHAIN_BENOIT:
         case GEOTIFF_LINEAR_CHAIN_SEARS:
         case GEOTIFF_LINEAR_YARD_SEARS:
         case GEOTIFF_LINEAR_YARD_INDIAN:
         case GEOTIFF_LINEAR_FATHOM:
         case GEOTIFF_LINEAR_MILE_INTERNATIONAL_NAUTICAL:
            convert_linear_units( proj_linear_units, proj_linear_unit_size,
                                  m_geodata.m_proj_false_easting );
            convert_linear_units( proj_linear_units, proj_linear_unit_size,
                                  m_geodata.m_proj_false_northing );
            convert_linear_units( proj_linear_units, proj_linear_unit_size,
                                  m_geodata.m_proj_false_origin_easting );
            convert_linear_units( proj_linear_units, proj_linear_unit_size,
                                  m_geodata.m_proj_false_origin_northing );
            convert_linear_units( proj_linear_units, proj_linear_unit_size,
                                  m_geodata.m_proj_center_easting );
            convert_linear_units( proj_linear_units, proj_linear_unit_size,
                                  m_geodata.m_proj_center_northing );
            break;

         case GEOTIFF_UNDEFINED:
            m_geodata_error_message = "Projection linear unit is undefined.";
            goto FAIL;
         default:
            m_geodata_error_message = "Projection linear unit is unknown.";
            goto FAIL;
      }
   }

   // convert azimuth angular units to degrees
   if( geog_azimuth_units_present )
   {
      // check for supported units
      switch( geog_azimuth_units )
      {
         case GEOTIFF_ANGULAR_RADIAN:
         case GEOTIFF_ANGULAR_DEGREE:
         case GEOTIFF_ANGULAR_ARC_MINUTE:
         case GEOTIFF_ANGULAR_ARC_SECOND:
         case GEOTIFF_ANGULAR_GRAD:
         case GEOTIFF_ANGULAR_GON:
            convert_angular_units( geog_azimuth_units, 0.0,
                                   m_geodata.m_proj_azimuth_angle );
            break;

         case GEOTIFF_ANGULAR_DMS:
            m_geodata_error_message =
               "Azimuth angular units not supported: DMS.";
            goto FAIL;
         case GEOTIFF_ANGULAR_DMS_HEMISPHERE:
            m_geodata_error_message =
               "Azimuth angular units not supported: DMS Hemisphere.";
            goto FAIL;
         case GEOTIFF_USER_DEFINED:
            m_geodata_error_message =
               "User-defined azimuth unit is not supported.";
            goto FAIL;
         case GEOTIFF_UNDEFINED:
            m_geodata_error_message = "Azimuth unit is undefined.";
            goto FAIL;
         default:
            m_geodata_error_message = "Azimuth unit is unknown.";
            goto FAIL;
      }
   }

   // add support for projected file with improper model type value
   if (m_geodata.m_projected_cs_type_present)
	   m_geodata.m_gt_model_type = GEOTIFF_MODEL_TYPE_PROJECTED;

   // test for model type
   switch( m_geodata.m_gt_model_type )
   {
      case GEOTIFF_MODEL_TYPE_PROJECTED:
         // projection coordinate system must be present
         if( !m_geodata.m_projected_cs_type_present )
         {
            m_geodata_error_message =
               "Projection coordinate system geokey is missing.";
            goto FAIL;
         }
         // check for user-defined projection coordinate_system
         if( m_geodata.m_projected_cs_type == GEOTIFF_USER_DEFINED )
         {
            if( !m_geodata.m_proj_coord_trans_present )
            {
               m_geodata_error_message =
               "User-defined projection coordinate transformation not present.";
               goto FAIL;
            }
            switch( m_geodata.m_proj_coord_trans )
            {
               case GEOTIFF_CT_LAMBERT_CONF_CONIC_2SP:
                  // check for necessary data.  The origin of a 2SP Lambert is
                  // ProjFalseOrigin{Lat,Long} (3084/3085) per the GeoTIFF
                  // spec - ProjNatOrigin* (3080/3081) is the 1SP spelling.
                  // Older libgeotiff output wrote the natural-origin keys
                  // here, and current FAA sectionals write the false-origin
                  // ones, so either spelling of either axis is accepted.
                  if( !m_geodata.m_proj_std_parallel_1_present )
                  {
                     m_geodata_error_message =
                        "Standard parallel 1 is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_std_parallel_2_present )
                  {
                     m_geodata_error_message =
                        "Standard parallel 2 is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_nat_origin_long_present &&
                      !m_geodata.m_proj_false_origin_long_present )
                  {
                     m_geodata_error_message =
                        "Origin longitude is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_false_origin_lat_present &&
                      !m_geodata.m_proj_nat_origin_lat_present )
                  {
                     m_geodata_error_message =
                        "Origin latitude is missing.";
                     goto FAIL;
                  }
                  m_geodata_supported = TRUE;
                  break;
               case GEOTIFF_CT_ALBERS_EQUAL_AREA:
                  // check for necessary data
                  if( !m_geodata.m_proj_std_parallel_1_present )
                  {
                     m_geodata_error_message =
                        "Standard parallel 1 is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_std_parallel_2_present )
                  {
                     m_geodata_error_message =
                        "Standard parallel 2 is missing.";
                     goto FAIL;
                  }
                  if( !m_geodata.m_proj_nat_origin_lat_present )
                  {
                     m_geodata_error_message =
                        "Natural origin latitude is missing.";
                     goto FAIL;
                  }

				  // this is not strictly required
//                  if( !m_geodata.m_proj_center_lat_present )
//                  {
//                     m_geodata_error_message = "Center latitude is missing.";
//                     goto FAIL;
//                  }

                  // we should test for center longitude, but the sample
                  // acea.tif doesn't include center longitude. so, to be able
                  // to load this sample, we disable this test and 0.0 is used
                  // for center longitude
//                  if( !m_geodata.m_proj_center_long_present )
//                  {
//                     m_geodata_error_message = "Center longitude is missing.";
//                     goto FAIL;
//                  }
                  m_geodata_supported = TRUE;
                  break;
 			   case GEOTIFF_CT_EQUIRECTANGULAR:
				   m_geodata_supported = TRUE;
				   break;
              default:
                  // user-defined
                  m_geodata_supported = FALSE;
                  break;
            }
         }
         else
         {
            // pre-defined projection coordinate system

            if( m_geodata.m_projected_cs_type >= 26703 &&
                m_geodata.m_projected_cs_type <= 26722 )
            {
               // NAD27 UTM northern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_NAD27;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 0.0;
               m_geodata.m_proj_false_northing_present = TRUE;
               m_geodata_supported = TRUE;
               break;
            }

            if( m_geodata.m_projected_cs_type >= 26903 &&
                m_geodata.m_projected_cs_type <= 26923 )
            {
               // NAD83 UTM northern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_NAD83;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 0.0;
               m_geodata.m_proj_false_northing_present = TRUE;
               m_geodata_supported = TRUE;
               break;
            }

            if( m_geodata.m_projected_cs_type >= 32201 &&
                m_geodata.m_projected_cs_type <= 32260 )
            {
               // WGS72 UTM northern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_WGS_72;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 0.0;
               m_geodata.m_proj_false_northing_present = TRUE;
               m_geodata_supported = TRUE;
               break;
            }

            if( m_geodata.m_projected_cs_type >= 32301 &&
                m_geodata.m_projected_cs_type <= 32360 )
            {
               // WGS72 UTM southern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_WGS_72;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 10000000.0;
               m_geodata.m_proj_false_northing_present = TRUE;
               m_geodata_supported = TRUE;
               break;
            }

            if( m_geodata.m_projected_cs_type >= 32601 &&
                m_geodata.m_projected_cs_type <= 32660 )
            {
               // WGS84 UTM northern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_WGS_84;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 0.0;
               m_geodata.m_proj_false_northing_present = TRUE;
               m_geodata_supported = TRUE;
               break;
            }

            if( m_geodata.m_projected_cs_type >= 32701 &&
                m_geodata.m_projected_cs_type <= 32760 )
            {
               // WGS84 UTM southern hemisphere
               m_geodata.m_geographic_type = GEOTIFF_GCS_WGS_84;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata.m_proj_false_easting = 500000.0;
               m_geodata.m_proj_false_easting_present = TRUE;
               m_geodata.m_proj_false_northing = 10000000.0;
               m_geodata.m_proj_false_northing_present = TRUE;
               m_geodata_supported = TRUE;
               break;
            }

            if(( m_geodata.m_projected_cs_type >= 26729 && m_geodata.m_projected_cs_type <= 26798 ) ||
				( m_geodata.m_projected_cs_type >= 32001 && m_geodata.m_projected_cs_type <= 32060 ))
            {
               // state plane NAD27
               m_geodata.m_geographic_type = GEOTIFF_GCS_NAD27;
               m_geodata.m_geographic_type_present = TRUE;
               m_geodata_supported = TRUE;
//				m_projection_type = "State Plane";
               break;
            }

            if(( m_geodata.m_projected_cs_type >= 26929 && m_geodata.m_projected_cs_type <= 26998 ) ||
				( m_geodata.m_projected_cs_type >= 32100 && m_geodata.m_projected_cs_type <= 32161 ))
            {
               // state plane NAD83
               m_geodata.m_geographic_type = GEOTIFF_GCS_NAD83;
               m_geodata.m_geographic_type_present = TRUE;
//				m_projection_type = "State Plane";
               m_geodata_supported = TRUE;
               break;
            }

            m_geodata_supported = FALSE;
         }
         break;

      case GEOTIFF_MODEL_TYPE_GEOGRAPHIC:
         if( m_geodata.m_model_pixel_scale_present &&
             m_geodata.m_num_tie_points == 1 )
         {
            // direct lat/long
            m_geodata_supported = TRUE;
         }
         else
         if( m_geodata.m_num_tie_points >= 3 &&
             !m_geodata.m_model_pixel_scale_present )
         {
            // georeferenced by tiepoints only
            // set up tiepoint transform
            if( m_tiepoint_transform.define_tiepoints(
                m_geodata.m_num_tie_points, m_geodata.m_model_tie_point_hpix,
                m_geodata.m_model_tie_point_vpix, m_geodata.m_model_tie_point_y,
                m_geodata.m_model_tie_point_x ) != SUCCESS )
            {
               m_geodata_error_message =
                  "Tiepoint transformation is invalid.";
               goto FAIL;
            }
            m_geodata_supported = TRUE;
         }
         break;

      case GEOTIFF_MODEL_TYPE_GEOCENTRIC:
         m_geodata_error_message = "Geocentric model type is not supported.";
         goto FAIL;

      default:
         m_geodata_error_message = "Geometric model type is unknown.";
         goto FAIL;
   }

   // assign datum
   switch( m_geodata.m_geographic_type )
   {
      case GEOTIFF_GCS_NAD27:
         m_geodata.m_geog_geodetic_datum =
            GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1927;
         m_geodata.m_geog_geodetic_datum_present = TRUE;
         m_geodata.m_geog_ellipsoid = GEOTIFF_ELLIPSE_CLARKE_1866;
         m_geodata.m_geog_ellipsoid_present = TRUE;
         m_geodata.m_geog_semi_major_axis = 6378206.4;
         m_geodata.m_geog_semi_major_axis_present = TRUE;
         m_geodata.m_geog_semi_minor_axis = 6356583.8;
         m_geodata.m_geog_semi_minor_axis_present = TRUE;
         m_geodata.m_geog_inv_flattening = 294.98;
         m_geodata.m_geog_inv_flattening_present = TRUE;
         break;
      case GEOTIFF_GCS_NAD83:
         m_geodata.m_geog_geodetic_datum =
            GEOTIFF_DATUM_NORTH_AMERICAN_DATUM_1983;
         m_geodata.m_geog_geodetic_datum_present = TRUE;
         m_geodata.m_geog_ellipsoid = GEOTIFF_ELLIPSE_GRS_1980;
         m_geodata.m_geog_ellipsoid_present = TRUE;
         m_geodata.m_geog_semi_major_axis = 6378137.0;
         m_geodata.m_geog_semi_major_axis_present = TRUE;
         m_geodata.m_geog_semi_minor_axis = 6356752.314;
         m_geodata.m_geog_semi_minor_axis_present = TRUE;
         m_geodata.m_geog_inv_flattening = 298.2572221010;
         m_geodata.m_geog_inv_flattening_present = TRUE;
         break;
      case GEOTIFF_GCS_WGS_72:
         m_geodata.m_geog_geodetic_datum = GEOTIFF_DATUM_WGS72;
         m_geodata.m_geog_geodetic_datum_present = TRUE;
         m_geodata.m_geog_semi_major_axis = 6378135.0;
         m_geodata.m_geog_semi_major_axis_present = TRUE;
         m_geodata.m_geog_semi_minor_axis = 6356750.5;
         m_geodata.m_geog_semi_minor_axis_present = TRUE;
         m_geodata.m_geog_inv_flattening = 298.26;
         m_geodata.m_geog_inv_flattening_present = TRUE;
         break;
      case GEOTIFF_GCS_WGS_84:
      case GEOTIFF_GCSE_WGS84:
         m_geodata.m_geog_geodetic_datum = GEOTIFF_DATUM_WGS84;
         m_geodata.m_geog_geodetic_datum_present = TRUE;
         m_geodata.m_geog_ellipsoid = GEOTIFF_ELLIPSE_WGS_84;
         m_geodata.m_geog_ellipsoid_present = TRUE;
         m_geodata.m_geog_semi_major_axis = 6378137.0;
         m_geodata.m_geog_semi_major_axis_present = TRUE;
         m_geodata.m_geog_semi_minor_axis = 6356752.3;
         m_geodata.m_geog_semi_minor_axis_present = TRUE;
         m_geodata.m_geog_inv_flattening = 298.2572235630;
         m_geodata.m_geog_inv_flattening_present = TRUE;
         break;
      default:
         m_geodata_error_message =
            "Geographic coordinate system is not supported.";
         goto FAIL;
   }

   m_geodata_present = TRUE;
   m_geodata_error_code = GEOTIFF_GEODATA_OK;
   m_geodata_error_message = "";
   return;

FAIL:
   m_geodata_present = TRUE;
   m_geodata_supported = FALSE;
   m_geodata_error_code = GEOTIFF_GEODATA_ERROR;
   return;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::convert_linear_units( unsigned short linear_units,
                                    double linear_unit_size, double &value )
{
   // this function converts the value argument from the specified linear units
   // to meters
   // returns SUCCESS or FAILURE (fails if linear units are not supported)

   switch( linear_units )
   {
      case GEOTIFF_LINEAR_METER:
         value *= 1.0;
         break;
      case GEOTIFF_LINEAR_FOOT:
         value *= 0.3048;
         break;
      case GEOTIFF_LINEAR_FOOT_US_SURVEY:
         value *= 12.0 / 39.37;
         break;
      case GEOTIFF_LINEAR_FOOT_MODIFIED_AMERICAN:
         value *= 0.3048 * 1.000040166747;
         break;
      case GEOTIFF_LINEAR_FOOT_CLARKE:
         value *= 12.0 / 39.370432;
         break;
      case GEOTIFF_LINEAR_FOOT_INDIAN:
         value *= 12.0 / 39.370142;
         break;
      case GEOTIFF_LINEAR_LINK:
         value *= 7.92 / 39.370432;
         break;
      case GEOTIFF_LINEAR_LINK_BENOIT:
         value *= 7.92 / 39.370113;
         break;
      case GEOTIFF_LINEAR_LINK_SEARS:
         value *= 7.92 / 39.370147;
         break;
      case GEOTIFF_LINEAR_CHAIN_BENOIT:
         value *= 792.0 / 39.370113;
         break;
      case GEOTIFF_LINEAR_CHAIN_SEARS:
         value *= 792.0 / 39.370147;
         break;
      case GEOTIFF_LINEAR_YARD_SEARS:
         value *= 36.0 / 39.370147;
         break;
      case GEOTIFF_LINEAR_YARD_INDIAN:
         value *= 36.0 / 39.370141;
         break;
      case GEOTIFF_LINEAR_FATHOM:
         value *= 6.0 * 0.3048;
         break;
      case GEOTIFF_LINEAR_MILE_INTERNATIONAL_NAUTICAL:
         value *= 1852.0;
         break;
      case GEOTIFF_USER_DEFINED:
         value *= linear_unit_size;
         break;
      case GEOTIFF_UNDEFINED:
      default:
         goto FAIL;
   }

   return SUCCESS;

FAIL:
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::convert_angular_units( unsigned short angular_units,
                           double angular_unit_size, double &value )
{
   // this function converts the value argument from the specified
   // angular units to degrees
   // returns SUCCESS or FAILURE (fails if angular units are not supported)

   double deg_per_rad = 180.0 / 4.0 / atan( 1.0 );

   switch( angular_units )
   {
      case GEOTIFF_ANGULAR_RADIAN:
         value *= deg_per_rad;
         break;
      case GEOTIFF_ANGULAR_DEGREE:
         value *= 1.0;
         break;
      case GEOTIFF_ANGULAR_ARC_MINUTE:
         value /= 60.0;
         break;
      case GEOTIFF_ANGULAR_ARC_SECOND:
         value /= 3600.0;
         break;
      case GEOTIFF_ANGULAR_GRAD:
         value *= 0.9;
         break;
      case GEOTIFF_ANGULAR_GON:
         value *= 0.9;
         break;
      case GEOTIFF_USER_DEFINED:
         value *= angular_unit_size * deg_per_rad;
         break;
      case GEOTIFF_ANGULAR_DMS:
      case GEOTIFF_ANGULAR_DMS_HEMISPHERE:
      case GEOTIFF_UNDEFINED:
      default:
         goto FAIL;
   }

   return SUCCESS;

FAIL:
   return FAILURE;
}

// **************************************************************************
// **************************************************************************

// returns TRUE if the lon1 is east of lon2
BOOL CGeoTiffFrameFile::is_east_of(double lon1, double lon2)
{
   double diff;

   /* (a>=b) and in same hemisphere, 0<=diff<= half_world 
      (a<=b) and in same hemisphere, 0>=diff>=-half_world
      (a>b) and in Opp hemispheres,  0< diff<= around_world 
      (a<b) and in Opp hemispheres,  0> diff>=-around_world */

   diff = lon1 - lon2;

   if (diff < 0.0) /* convert diff to eastward distance from b to a */
      diff += 360.0;

   if (0.0 < diff && diff <= 180.0)
      return TRUE;

   return FALSE;
} 

// **************************************************************************
// **************************************************************************

#ifdef _WIN32  // ImageLib COM fallback (MrSID etc.)
int CGeoTiffFrameFile::imagelib_load( IImageLibPtr & imagelib, std::string file_name, std::string & datum_str,
									 int &image_width, int &image_length, std::string &error_msg )
{
	BSTR berr_msg, binfo;
	int err_code, spp, rslt;
	double ul_lat, ul_lon, ur_lat, ur_lon, lr_lat, lr_lon, ll_lat, ll_lon;
	std::string info, data;
	double minlat, maxlat, minlon, maxlon;

	image_width = 0;
	image_length = 0;
	spp = 0;

	try
	{
		imagelib->close();
		imagelib->load(_bstr_t(file_name.c_str()), &image_width, &image_length, &err_code, &berr_msg);
		if (err_code != 0)
		{
			error_msg = _bstr_t(berr_msg);
			return FAILURE;
		}
		imagelib->get_corner_coords(&ul_lat, &ul_lon, &ur_lat, &ur_lon,
									&lr_lat, &lr_lon, &ll_lat, &ll_lon, &err_code);
		if (err_code != 0)
		{
			error_msg = "No supported geo-referencing";
			return FAILURE;
		}

		// find min and max of the corners (image could be rotated)
		minlat = maxlat = ll_lat;
		minlon = maxlon = ll_lon;
		if (ur_lat < minlat)
			minlat = ur_lat;
		if (ur_lat > maxlat)
			maxlat = ur_lat;
		if (lr_lat < minlat)
			minlat = lr_lat;
		if (lr_lat > maxlat)
			maxlat = lr_lat;
		if (ul_lat < minlat)
			minlat = ul_lat;
		if (is_east_of(minlon, ur_lon))
			minlon = ur_lon;
		if (is_east_of(ur_lon, maxlon))
			minlon = maxlon;
		if (is_east_of(minlon, lr_lon))
			minlon = lr_lon;
		if (is_east_of(lr_lon, maxlon))
			maxlon = lr_lon;
		if (is_east_of(minlon, ul_lon))
			minlon = ul_lon;
		if (is_east_of(ul_lon, maxlon))
			maxlon = ul_lon;

		m_ll_lat = minlat;
		m_ll_lon = minlon;
		m_ur_lat = maxlat;
		m_ur_lon = maxlon;

		imagelib->get_info(&binfo, &err_code, &berr_msg);
		info = _bstr_t(binfo);
		rslt = extract_data_from_info(info, "Datum", &data);
		datum_str = data;
//		data = extract_info_data("X Resolution: ", info);
//		m_x_resolution = atof(data.c_str());
//		data = extract_info_data("Y Resolution: ", info);
//		m_y_resolution = atof(data.c_str());
//		m_creation_date_str = extract_info_data("DateTime: ", info);
		rslt = extract_data_from_info(info, "Samples Per Pixel", &data);
		if (rslt == SUCCESS)
			spp = static_cast<int>(atof(data.c_str()));
		else
		{
			rslt = extract_data_from_info(info, "Num Bands", &data);
			if (rslt == SUCCESS)
				spp = static_cast<int>(atof(data.c_str()));
		}
//		data = extract_info_data("Bits Per Sample: ", info);
//		bps = static_cast<int>(atof(data.c_str()));

		if (spp == 1)
		{
			std::string bw("B&W");
			m_series_str = bw;
		}
		else
		{
			std::string col("Color");
			m_series_str = col;
		}
/*
	*sScaleUnits = MAP_SCALE_METERS;
	*series = _bstr_t(BLACK_AND_WHITE_SERIES).copy();
	if (spp > 1)
	{
		::SysFreeString(*series);
		*series = _bstr_t(COLOR_SERIES).copy();
	}
*/

	}
	catch(...)
	{
		error_msg = "ImageLib Error";
		return FAILURE;
	}

	m_width = image_width;
	m_height = image_length;

	return SUCCESS;
}
// end of imagelib_load

// ******************************************************************************
// ******************************************************************************

// convert lat/long to image coordinates
// image must first have been loaded using the same ImageLib pointer

int CGeoTiffFrameFile::imagelib_fwd_transform( IImageLibPtr & imagelib,	std::string datum_string,
							   double latitude, double longitude, int *hpix, int *vpix )
{
	double sid_datum_lat, sid_datum_lon;

	int err_code;
	BSTR berr_msg;
	IDatumConvertPtr geotrans;


   BSTR bstrDatumString = _bstr_t(datum_string.c_str()).Detach();
	// first convert the lat/lon to the geotiff's datum
	geotrans.CreateInstance(__uuidof(DatumConvert));

	try
	{
		geotrans->ConvertDatum(latitude, longitude, &sid_datum_lat, &sid_datum_lon, _bstr_t(_T("W84")), bstrDatumString);

		imagelib->geo_to_image(sid_datum_lat, sid_datum_lon, hpix, vpix, &err_code, &berr_msg);
		if (err_code == 0)
			return SUCCESS;
		else
			return FAILURE;
	}
	catch(...)
	{
		return FAILURE;
	}
}
// end of imagelib_fwd_transform

// **************************************************************************
// **************************************************************************

// convert image coordinates to lat/long
// image must first have been loaded using the same ImageLib pointer

int CGeoTiffFrameFile::imagelib_inv_transform( IImageLibPtr & imagelib,	std::string datum_string,
							   int hpix, int vpix, double *latitude, double *longitude )
{
	double sid_datum_lat, sid_datum_lon;
	int err_code;
	BSTR berr_msg;
	IDatumConvertPtr geotrans;

	try
	{
		imagelib->image_to_geo(hpix, vpix, &sid_datum_lat, &sid_datum_lon, &err_code, &berr_msg);
		if (err_code != 0)
			return FAILURE;
	}
	catch(...)
	{
		return FAILURE;
	}


	// now convert to WGS84 datum
	geotrans.CreateInstance(__uuidof(DatumConvert));

	geotrans->ConvertDatum(sid_datum_lat, sid_datum_lon, latitude, longitude, _bstr_t(datum_string.c_str()), _bstr_t(_T("W84")) );

	return SUCCESS;
}
// end of imagelib_inv_transform
#endif  // _WIN32 (ImageLib/MrSID fallback helpers)

// **************************************************************************
// **************************************************************************

int CGeoTiffFrameFile::inv_transform( int hpix, int vpix, double &latitude,
                             double &longitude )
{
   // this function returns the latitude and longitude in degrees for the
   // specified pixel. the pixel must lie within the image.
   // returns SUCCESS or FAILURE

   unsigned short pcs;

   // return failure if no file loaded or there was an error loading
   // the tiff file or if geodata is not supported
   if( !m_file_loaded || m_error || !m_geodata_supported )
	   goto FAIL;

   // test for pixel within image bounds
   if( hpix < 0 || hpix >= (int)m_image_width || vpix < 0 ||
       vpix >= (int)m_image_length )
	   goto FAIL;

  // test for model type
   switch( m_geodata.m_gt_model_type )
   {
      case GEOTIFF_MODEL_TYPE_PROJECTED:
         // check for user-defined projection coordinate_system
         if( m_geodata.m_projected_cs_type == GEOTIFF_USER_DEFINED )
         {
            switch( m_geodata.m_proj_coord_trans )
            {
               case GEOTIFF_CT_LAMBERT_CONF_CONIC_2SP:
                  if( inv_transform_lambert_conf_conic( hpix, vpix,
                      latitude, longitude ) != SUCCESS )
					  goto FAIL;
                  break;
               case GEOTIFF_CT_ALBERS_EQUAL_AREA:
                  if( inv_transform_albers_equal_area( hpix, vpix,
                      latitude, longitude ) != SUCCESS )
					  goto FAIL;
                  break;
 			   case GEOTIFF_CT_EQUIRECTANGULAR:
				  if (inv_transform_lat_long(hpix, vpix, latitude, longitude) != SUCCESS)
					  goto FAIL;
				  break;
              default:
                  goto FAIL;
            }
            break;
         }
         else
         {
            // pre-defined projection coordinate system
            pcs = m_geodata.m_projected_cs_type;
            if( pcs >= 26703 && pcs <= 26722 ||
                pcs >= 26903 && pcs <= 26922 ||
                pcs >= 32201 && pcs <= 32260 ||
                pcs >= 32301 && pcs <= 32360 ||
                pcs >= 32601 && pcs <= 32660 ||
                pcs >= 32701 && pcs <= 32760 )
            {
               if( inv_transform_utm( hpix, vpix, latitude, longitude ) != SUCCESS )
				   goto FAIL;
               break;
            }
            else if( pcs >= 26729 && pcs <= 26798 ||
					pcs >= 32001 && pcs <= 32060 ||
					pcs >= 26929 && pcs <= 26998 ||
					pcs >= 32100 && pcs <= 32161 )
            {
               if( inv_transform_sp( hpix, vpix, latitude, longitude ) != SUCCESS )
				   goto FAIL;
               break;
            }
            else
               goto FAIL;
         }

      case GEOTIFF_MODEL_TYPE_GEOGRAPHIC:
         if( m_geodata.m_model_pixel_scale_present &&
             m_geodata.m_num_tie_points == 1 )
         {
            // direct lat/long
           if( inv_transform_lat_long( hpix, vpix, latitude, longitude ) !=
               SUCCESS )
               goto FAIL;
         }
         else
         if( m_geodata.m_num_tie_points >= 3 &&
             !m_geodata.m_model_pixel_scale_present )
         {
            // georeferenced by tiepoints only
            if( m_tiepoint_transform.inv_transform( (double)hpix, (double)vpix,
               latitude, longitude ) != SUCCESS )
               goto FAIL;
         }
         break;

      case GEOTIFF_MODEL_TYPE_GEOCENTRIC:
          goto FAIL;

      default:
          goto FAIL;
   }

   return SUCCESS;

FAIL:
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::fwd_transform( double latitude, double longitude, int &hpix,
                             int &vpix )
{
   // this function returns the pixel which corresponds to the specified
   // latitude and longitude, provided it lies within the image
   // returns SUCCESS or FAILURE

   unsigned short pcs;
   double x, y;

   // return failure if no file loaded or there was an error loading the
   // tiff file or if geodata is not supported
   if( !m_file_loaded || m_error || !m_geodata_supported )
	   goto FAIL;

   // test for model type
   switch( m_geodata.m_gt_model_type )
   {
      case GEOTIFF_MODEL_TYPE_PROJECTED:
         // check for user-defined projection coordinate_system
         if( m_geodata.m_projected_cs_type == GEOTIFF_USER_DEFINED )
         {
            switch( m_geodata.m_proj_coord_trans )
            {
               case GEOTIFF_CT_LAMBERT_CONF_CONIC_2SP:
                  if( fwd_transform_lambert_conf_conic( latitude, longitude,
                      hpix, vpix ) != SUCCESS )
					  goto FAIL;
                  break;
               case GEOTIFF_CT_ALBERS_EQUAL_AREA:
                  if( fwd_transform_albers_equal_area( latitude, longitude,
                      hpix, vpix ) != SUCCESS )
					  goto FAIL;
                  break;
			   case GEOTIFF_CT_EQUIRECTANGULAR:
                  if( fwd_transform_lat_long( latitude, longitude, hpix, vpix ) != SUCCESS )
					  goto FAIL;
                  break;
               default:
                  goto FAIL;
            }
         }
         else
         {
            // pre-defined projection coordinate system
            pcs = m_geodata.m_projected_cs_type;
            if( pcs >= 26703 && pcs <= 26722 ||
                pcs >= 26903 && pcs <= 26922 ||
                pcs >= 32201 && pcs <= 32260 ||
                pcs >= 32301 && pcs <= 32360 ||
                pcs >= 32601 && pcs <= 32660 ||
                pcs >= 32701 && pcs <= 32760 )
            {
               if( fwd_transform_utm( latitude, longitude, hpix, vpix) != SUCCESS )
				   goto FAIL;
               break;
            }
            else if( pcs >= 26729 && pcs <= 26798 ||
					pcs >= 32001 && pcs <= 32060 ||
					pcs >= 26929 && pcs <= 26998 ||
					pcs >= 32100 && pcs <= 32161 )
            {
               if ( fwd_transform_sp( latitude, longitude, hpix, vpix ) != SUCCESS )
				   goto FAIL;
               break;
            }
            else
               goto FAIL;
         }
         break;

      case GEOTIFF_MODEL_TYPE_GEOGRAPHIC:
         if( m_geodata.m_model_pixel_scale_present &&
             m_geodata.m_num_tie_points == 1 )
         {
            // direct lat/long
            if( fwd_transform_lat_long( latitude, longitude, hpix, vpix) !=
                SUCCESS )
                goto FAIL;
         }
         else
         if( m_geodata.m_num_tie_points >= 3 &&
             !m_geodata.m_model_pixel_scale_present )
         {
            // georeferenced by tiepoints only
            if( m_tiepoint_transform.fwd_transform( latitude, longitude,
               x, y ) != SUCCESS )
               goto FAIL;
            if( x >= 0.0 )
               hpix = (int)(x + 0.5);
            else
               hpix = (int)(x - 0.5);
            if( y >= 0.0 )
               vpix = (int)(y + 0.5);
            else
               vpix = (int)(y - 0.5);
         }
         break;

      default:
          goto FAIL;
   }

   return SUCCESS;

FAIL:
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_image_bounds( double &lat_upper_left, double &long_upper_left,
                                double &lat_lower_left, double &long_lower_left,
                                double &lat_lower_right,
                                double &long_lower_right,
                                double &lat_upper_right,
                                double &long_upper_right )
{
   // this function returns the latitude and longitude in degrees for the four
   // image corner points
   // returns SUCCESS or FAILURE

   // return failure if no file loaded or there was an error loading
   // the tiff file or if geodata is not supported
   if( !m_file_loaded || m_error || !m_geodata_supported )
	   goto FAIL;

   // upper left corner
   if( inv_transform( 0, 0, lat_upper_left, long_upper_left ) != SUCCESS )
	   goto FAIL;

   // lower left corner
   if( inv_transform( 0, m_image_length-1, lat_lower_left, long_lower_left ) != SUCCESS )
	   goto FAIL;

   // lower right corner
   if( inv_transform( m_image_width-1, m_image_length-1,
       lat_lower_right, long_lower_right ) != SUCCESS )
	   goto FAIL;

   // upper right corner
   if( inv_transform( m_image_width-1, 0,
       lat_upper_right, long_upper_right ) != SUCCESS )
	   goto FAIL;

   return SUCCESS;

FAIL:
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::inv_transform_utm( int hpix, int vpix, double &latitude,
                                 double &longitude )
{
   // performs inverse transform for UTM projection using data in m_geodata
   // returns latitude and longitude in degrees for specified pixel
   // returns SUCCESS or FAILURE
   int zone;
   double x, y;
   double k0 = 0.9996;
   double phi0 = 0.0;
   double pi_over_180 = 4.0 * atan( 1.0 ) / 180.0;
   double lambda0, phi1;
   double m, m0;
   double a, b, f, e_sqrd, e_prime_sqrd, e1, mu;
   double c1, t1, n1, r1, d;
   BOOL south = FALSE;

   // find zone (1-60)
   if( m_geodata.m_projected_cs_type >= 26703 &&
       m_geodata.m_projected_cs_type <= 26722 )
   {
      zone = m_geodata.m_projected_cs_type - 26700;
      goto CALC_XY;
   }

   if( m_geodata.m_projected_cs_type >= 26903 &&
       m_geodata.m_projected_cs_type <= 26923 )
   {
      zone = m_geodata.m_projected_cs_type - 26900;
      goto CALC_XY;
   }

   if( m_geodata.m_projected_cs_type >= 32201 &&
       m_geodata.m_projected_cs_type <= 32260 )
   {
      zone = m_geodata.m_projected_cs_type - 32200;
      goto CALC_XY;
   }

   if( m_geodata.m_projected_cs_type >= 32301 &&
       m_geodata.m_projected_cs_type <= 32360 )
   {
      zone = m_geodata.m_projected_cs_type - 33200;
      goto CALC_XY;
   }

   if( m_geodata.m_projected_cs_type >= 32601 &&
       m_geodata.m_projected_cs_type <= 32660 )
   {
      zone = m_geodata.m_projected_cs_type - 32600;
      goto CALC_XY;
   }

   if( m_geodata.m_projected_cs_type >= 32701 &&
       m_geodata.m_projected_cs_type <= 32760 )
   {
      zone = m_geodata.m_projected_cs_type - 32700;
	  south = TRUE;
      goto CALC_XY;
   }

   // unknown range, return failure
   goto FAIL;

CALC_XY:
   x = m_geodata.m_model_tie_point_x[0] + m_geodata.m_model_pixel_scale_x *
       ( (double)hpix - m_geodata.m_model_tie_point_hpix[0] );
   y = m_geodata.m_model_tie_point_y[0] - m_geodata.m_model_pixel_scale_y *
       ( (double)vpix - m_geodata.m_model_tie_point_vpix[0] );

   // subtract false easting from x and false northing from y
   x -= m_geodata.m_proj_false_easting;
//   y -= m_geodata.m_proj_false_northing;
	if (south)
		y = 10000000.0 - y;

   // calculate flatenning and eccentricity
   a = m_geodata.m_geog_semi_major_axis;
   b = m_geodata.m_geog_semi_minor_axis;
   f = ( a - b ) / a;
   e_sqrd = 2*f - f*f;

   lambda0 = ( -183.0 + zone * 6.0 ) * pi_over_180;

   m0 = 111132.0894*phi0 -16216.94*sin(2*phi0) +
        17.21*sin(4*phi0)-0.02*sin(6*phi0);

   e_prime_sqrd = e_sqrd / ( 1.0-e_sqrd);
   m = m0 + y / k0;
   e1 = (1.0-sqrt(1.0-e_sqrd)) / (1.0+sqrt(1.0-e_sqrd));
   mu = m /
        ( a * (1-e_sqrd/4 -3*e_sqrd*e_sqrd/64 -5*e_sqrd*e_sqrd*e_sqrd/256 ) );
   phi1 = mu +
          ( 3*e1/2 - 27*pow(e1,3.0) ) * sin( 2*mu ) +
          ( 21*e1*e1/16 - 55*pow(e1,4.0)/32 ) * sin( 4*mu ) +
          ( 151*pow(e1,3.0)/96 ) * sin( 6*mu ) +
          ( 1097*pow(e1,4.0)/512 ) * sin( 8*mu );

   c1 = e_prime_sqrd * cos( phi1 ) * cos( phi1 );
   t1 = tan( phi1 ) * tan( phi1 );
   n1 = a / sqrt( 1 - e_sqrd * sin( phi1 ) * sin( phi1 ) );
   r1 = a * (1-e_sqrd) / pow( 1 - e_sqrd * sin( phi1 ) * sin( phi1 ), 1.5 );
   d = x / n1 / k0;

   latitude = phi1 - ( n1* tan(phi1) / r1 ) *
              (
                 d*d/2 -
                 ( 5 + 3*t1 + 10*c1 -4*c1*c1 - 9*e_prime_sqrd ) *
                   pow(d,4.0)/24 +
                 ( 61 + 90*t1 + 298*c1 + 45*t1*t1 - 252*e_prime_sqrd -
                   3*c1*c1 ) * pow(d,6.0)/720
              );

   longitude = lambda0 + ( d - (1 + 2*t1 + c1)*pow(d,3.0)/6 +
               (5 - 2*c1 + 28*t1 - 3*c1*c1 + 8*e_prime_sqrd + 24*t1*t1) *
               pow(d,5.0)/120 ) / cos( phi1 );

   // convert from radians to degrees
   latitude /= pi_over_180;
   longitude /= pi_over_180;

   if (longitude < -180.0)
	   longitude += 360.0;
   if (longitude > 180.0)
	   longitude -= 360.0;

   if (south)
	   latitude = -latitude;

   return SUCCESS;

FAIL:
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::fwd_transform_utm( double latitude, double longitude,
                                 int &hpix, int &vpix )
{
   // performs forward transform for UTM projection using data in m_geodata
   // returns pixel coordinates for specified latitude and longitude
   // returns SUCCESS or FAILURE

   int zone;
   double x, y, dhpix, dvpix;
   double k0 = 0.9996;
   double phi0 = 0.0;
   double pi_over_180 = 4.0 * atan( 1.0 ) / 180.0;
   double lambda0, lambda, phi;
   double m, m0;
   double a, b, f, e_sqrd, e_prime_sqrd;
   double c, t, n, aa;

   // find zone (1-60)
   if( m_geodata.m_projected_cs_type >= 26703 &&
       m_geodata.m_projected_cs_type <= 26722 )
   {
      zone = m_geodata.m_projected_cs_type - 26700;
      goto CALC_GEOID;
   }

   if( m_geodata.m_projected_cs_type >= 26903 &&
       m_geodata.m_projected_cs_type <= 26923 )
   {
      zone = m_geodata.m_projected_cs_type - 26900;
      goto CALC_GEOID;
   }

   if( m_geodata.m_projected_cs_type >= 32201 &&
       m_geodata.m_projected_cs_type <= 32260 )
   {
      zone = m_geodata.m_projected_cs_type - 32200;
      goto CALC_GEOID;
   }

   if( m_geodata.m_projected_cs_type >= 32301 &&
       m_geodata.m_projected_cs_type <= 32360 )
   {
      zone = m_geodata.m_projected_cs_type - 33200;
      goto CALC_GEOID;
   }

   if( m_geodata.m_projected_cs_type >= 32601 &&
       m_geodata.m_projected_cs_type <= 32660 )
   {
      zone = m_geodata.m_projected_cs_type - 32600;
      goto CALC_GEOID;
   }

   if( m_geodata.m_projected_cs_type >= 32701 &&
       m_geodata.m_projected_cs_type <= 32760 )
   {
      zone = m_geodata.m_projected_cs_type - 32700;
      goto CALC_GEOID;
   }

   // unknown range, return failure
   goto FAIL;

CALC_GEOID:
   // calculate flatenning and eccentricity
   a = m_geodata.m_geog_semi_major_axis;
   b = m_geodata.m_geog_semi_minor_axis;
   f = ( a - b ) / a;
   e_sqrd = 2*f - f*f;
   e_prime_sqrd = e_sqrd / ( 1.0-e_sqrd);

   // latitude and longitude in radians
   phi = latitude * pi_over_180;
   lambda = longitude * pi_over_180;

   if( fabs( latitude ) < 89.9 )       // equations blow up at poles
   {
      lambda0 = ( -183.0 + zone * 6.0 ) * pi_over_180;

      n = a / sqrt( 1 - e_sqrd * sin( phi ) * sin( phi ) );
      t = tan( phi ) * tan( phi );
      c = e_prime_sqrd * cos( phi ) * cos( phi );
      aa = ( lambda - lambda0 ) * cos( phi );
      m = a * (
            (1 - e_sqrd/4 - 3*e_sqrd*e_sqrd/64 - 5*e_sqrd*e_sqrd*e_sqrd/256)*
            phi -
            (3*e_sqrd/8 + 3*e_sqrd*e_sqrd/32 + 45*e_sqrd*e_sqrd*e_sqrd/1024)*
            sin(2*phi) +
            (15*e_sqrd*e_sqrd/256 + 45*e_sqrd*e_sqrd*e_sqrd/1024)*sin(4*phi) -
            (35*e_sqrd*e_sqrd*e_sqrd/3072)*sin(6*phi) );

      m0 = 111132.0894*phi0 - 16216.94*sin(2*phi0) + 17.21*sin(4*phi0) -
           0.02*sin(6*phi0);

      x = k0*n*( aa +
                 (1-t+c)*pow(aa,3.)/6 +
                 (5-18*t+t*t+72*c-58*e_prime_sqrd)*pow(aa,5.)/120 );
      y = k0*( m - m0 + n*tan(phi)*(
               aa*aa/2 + (5-t+9*c+4*c*c)*pow(aa,4.)/24 +
               (61-58*t+t*t+600*c-330*e_prime_sqrd)*pow(aa,6.)/720) );
   }
   else
   {
      m = a * (
            (1 - e_sqrd/4 - 3*e_sqrd*e_sqrd/64 - 5*e_sqrd*e_sqrd*e_sqrd/256) *
            phi -
            (3*e_sqrd/8 + 3*e_sqrd*e_sqrd/32 + 45*e_sqrd*e_sqrd*e_sqrd/1024) *
            sin(2*phi) +
            (15*e_sqrd*e_sqrd/256 + 45*e_sqrd*e_sqrd*e_sqrd/1024)*sin(4*phi) -
            (35*e_sqrd*e_sqrd*e_sqrd/3072)*sin(6*phi) );

      m0 = 111132.0894*phi0 - 16216.94*sin(2*phi0) + 17.21*sin(4*phi0) -
           0.02*sin(6*phi0);

      x = 0.0;
      y = k0*( m - m0 );
   }

   // add false easting to x and false northing to y
   x += m_geodata.m_proj_false_easting;
//   y += m_geodata.m_proj_false_northing;
   if (latitude < 0.0)
	   y = 10000000.0 + y;

   dhpix = m_geodata.m_model_tie_point_hpix[0] +
           (x - m_geodata.m_model_tie_point_x[0]) /
           m_geodata.m_model_pixel_scale_x;
   dvpix = m_geodata.m_model_tie_point_vpix[0] -
           (y - m_geodata.m_model_tie_point_y[0]) /
           m_geodata.m_model_pixel_scale_y;

   // round to nearest integer
   if( dhpix >= 0.0 )
      hpix = (int)(dhpix + 0.5);
   else
      hpix = (int)(dhpix - 0.5);
   if( dvpix >= 0.0 )
      vpix = (int)(dvpix + 0.5);
   else
      vpix = (int)(dvpix - 0.5);

   return SUCCESS;

FAIL:
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::fwd_transform_sp( double latitude, double longitude, int &hpix, int &vpix  )
{
	// performs inverse transform for State Plane projection using data in m_geodata
	// returns latitude and longitude in degrees for specified pixel
	// returns SUCCESS or FAILURE
	int rslt;
	double x, y, dhpix, dvpix;
	CProj proj;

	rslt = proj.set_proj_type(PROJ_TYPE_SPCS, m_geodata.m_projected_cs_type);
	if (rslt != SUCCESS)
		return FAILURE;

	switch(m_geog_linear_units)
	{
		case GEOTIFF_LINEAR_METER:
			proj.set_units(PROJ_UNITS_METERS);
			break;
		case GEOTIFF_LINEAR_FOOT:
		case GEOTIFF_LINEAR_FOOT_US_SURVEY:
		case GEOTIFF_LINEAR_FOOT_MODIFIED_AMERICAN:
		case GEOTIFF_LINEAR_FOOT_CLARKE:
		case GEOTIFF_LINEAR_FOOT_INDIAN:
			proj.set_units(PROJ_UNITS_US_FEET);
			break;
	}

	rslt = proj.geo_to_xy(latitude, longitude, &x, &y);
	if (rslt != SUCCESS)
		return FAILURE;

//	if (m_linear_units > 0)
//		proj.set_units(m_linear_units);

   dhpix = m_geodata.m_model_tie_point_hpix[0] +
           (x - m_geodata.m_model_tie_point_x[0]) /
           m_geodata.m_model_pixel_scale_x;
   dvpix = m_geodata.m_model_tie_point_vpix[0] -
           (y - m_geodata.m_model_tie_point_y[0]) /
           m_geodata.m_model_pixel_scale_y;

   // round to nearest integer
   if( dhpix >= 0.0 )
      hpix = (int)(dhpix + 0.5);
   else
      hpix = (int)(dhpix - 0.5);
   if( dvpix >= 0.0 )
      vpix = (int)(dvpix + 0.5);
   else
      vpix = (int)(dvpix - 0.5);

   return SUCCESS;
}

// *****************************************************************
// ********************************************************************************************

int CGeoTiffFrameFile::inv_transform_sp( int hpix, int vpix, double &latitude, double &longitude )
{
	// performs inverse transform for State Plane projection using data in m_geodata
	// returns latitude and longitude in degrees for specified pixel
	// returns SUCCESS or FAILURE
	int rslt;
	double x, y, lat, lon;
	CProj proj;

	if (!m_geodata.m_model_tie_point_x || !m_geodata.m_model_tie_point_hpix ||
		!m_geodata.m_model_tie_point_y || !m_geodata.m_model_tie_point_vpix)
		return FAILURE;

	x = m_geodata.m_model_tie_point_x[0] + m_geodata.m_model_pixel_scale_x *
	   ( (double)hpix - m_geodata.m_model_tie_point_hpix[0] );
	y = m_geodata.m_model_tie_point_y[0] - m_geodata.m_model_pixel_scale_y *
	   ( (double)vpix - m_geodata.m_model_tie_point_vpix[0] );

	// the scaling factor scales to meters; convert to feet
//	x /= 0.3048;
//	y /= 0.3048;

	rslt = proj.set_proj_type(PROJ_TYPE_SPCS, m_geodata.m_projected_cs_type);
	if (rslt != SUCCESS)
		return FAILURE;

//	if (m_linear_units > 0)
//		proj.set_units(m_linear_units);

	switch(m_geog_linear_units)
	{
		case GEOTIFF_LINEAR_METER:
			proj.set_units(PROJ_UNITS_METERS);
			break;
		case GEOTIFF_LINEAR_FOOT:
		case GEOTIFF_LINEAR_FOOT_US_SURVEY:
		case GEOTIFF_LINEAR_FOOT_MODIFIED_AMERICAN:
		case GEOTIFF_LINEAR_FOOT_CLARKE:
		case GEOTIFF_LINEAR_FOOT_INDIAN:
			proj.set_units(PROJ_UNITS_US_FEET);
			break;
	}

	rslt = proj.xy_to_geo(x, y, &lat, &lon);
	if (rslt == SUCCESS)
	{
		latitude = lat;
		longitude = lon;
		return SUCCESS;
	}

   return FAILURE;
}

// ********************************************************************************************
// *****************************************************************

int CGeoTiffFrameFile::inv_transform_lat_long( int hpix, int vpix,
                                      double &latitude, double &longitude )
{
   // performs inverse transform for lat/long projection using data in m_geodata
   // returns latitude and longitude in degrees for specified pixel
   // returns SUCCESS or FAILURE

   longitude = m_geodata.m_model_tie_point_x[0] +
      m_geodata.m_model_pixel_scale_x *
      ( (double)hpix - m_geodata.m_model_tie_point_hpix[0] );
   latitude = m_geodata.m_model_tie_point_y[0] -
      m_geodata.m_model_pixel_scale_y *
      ( (double)vpix - m_geodata.m_model_tie_point_vpix[0] );

   // check for longitude limits
   if( longitude > 180.0 )
      longitude -= 360.0;
   if( longitude < -180.0 )
      longitude += 360.0;

   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::fwd_transform_lat_long( double latitude, double longitude,
                                      int &hpix, int &vpix )
{
   // performs forward transform for lat/long projection using data in m_geodata
   // returns pixel coordinates for specified latitude and longitude
   // returns SUCCESS or FAILURE

   double dhpix, dvpix;

   dhpix = m_geodata.m_model_tie_point_hpix[0] +
           (longitude - m_geodata.m_model_tie_point_x[0]) /
           m_geodata.m_model_pixel_scale_x;
   dvpix = m_geodata.m_model_tie_point_vpix[0] -
           (latitude - m_geodata.m_model_tie_point_y[0]) /
           m_geodata.m_model_pixel_scale_y;

   // round to nearest integer
   if( dhpix >= 0.0 )
      hpix = (int)(dhpix + 0.5);
   else
      hpix = (int)(dhpix - 0.5);
   if( dvpix >= 0.0 )
      vpix = (int)(dvpix + 0.5);
   else
      vpix = (int)(dvpix - 0.5);

   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::inv_transform_lambert_conf_conic( int hpix, int vpix,
                                                double &latitude,
                                                double &longitude )
{
   // performs inverse transform for lambert conformal conic projection
   // using data in m_geodata
   // returns latitude and longitude in degrees for specified pixel
   // returns SUCCESS or FAILURE

   int sign_n;
   double a, b, f, e_sqrd, e, x, y;
   double phi0, phi1, phi2, lambda0, theta, chi;
   double t, rho, rho0, F, n, m1, m2, t0, t1, t2;
   double pi_over_180 = 4.0 * atan( 1.0 ) / 180.0;
   double pi_over_4 = atan( 1.0 );

   x = m_geodata.m_model_tie_point_x[0] + m_geodata.m_model_pixel_scale_x *
       ( (double)hpix - m_geodata.m_model_tie_point_hpix[0] );
   y = m_geodata.m_model_tie_point_y[0] - m_geodata.m_model_pixel_scale_y *
       ( (double)vpix - m_geodata.m_model_tie_point_vpix[0] );

   // subtract false easting from x and false northing from y.  A 2SP Lambert
   // spells these ProjFalseOrigin{Easting,Northing} (3086/3087); fall back to
   // those when the 1SP keys (3082/3083) are absent.
   if( m_geodata.m_proj_false_easting_present ||
       !m_geodata.m_proj_false_origin_easting_present )
      x -= m_geodata.m_proj_false_easting;
   else
      x -= m_geodata.m_proj_false_origin_easting;
   if( m_geodata.m_proj_false_northing_present ||
       !m_geodata.m_proj_false_origin_northing_present )
      y -= m_geodata.m_proj_false_northing;
   else
      y -= m_geodata.m_proj_false_origin_northing;

   a = m_geodata.m_geog_semi_major_axis;
   b = m_geodata.m_geog_semi_minor_axis;
   f = ( a - b ) / a;
   e_sqrd = 2*f - f*f;
   e = sqrt( e_sqrd );

   // 2SP's origin is ProjFalseOrigin{Lat,Long}; ProjNatOrigin* is the 1SP
   // spelling, which older writers used here.  Take whichever is present -
   // a file that loads today has the same one it always had.
   if( m_geodata.m_proj_false_origin_lat_present )
      phi0 = m_geodata.m_proj_false_origin_lat * pi_over_180;
   else
      phi0 = m_geodata.m_proj_nat_origin_lat * pi_over_180;
   phi1 = m_geodata.m_proj_std_parallel_1 * pi_over_180;
   phi2 = m_geodata.m_proj_std_parallel_2 * pi_over_180;
   if( m_geodata.m_proj_nat_origin_long_present )
      lambda0 = m_geodata.m_proj_nat_origin_long * pi_over_180;
   else
      lambda0 = m_geodata.m_proj_false_origin_long * pi_over_180;

   m1 = cos( phi1 ) / sqrt( 1 - e_sqrd * sin( phi1 ) * sin( phi1 ) );
   m2 = cos( phi2 ) / sqrt( 1 - e_sqrd * sin( phi2 ) * sin( phi2 ) );

   t0 = tan( pi_over_4 - phi0/2 ) /
        pow( (1-e*sin(phi0)) / (1+e*sin(phi0)), e/2 );
   t1 = tan( pi_over_4 - phi1/2 ) /
        pow( (1-e*sin(phi1)) / (1+e*sin(phi1)), e/2 );
   t2 = tan( pi_over_4 - phi2/2 ) /
        pow( (1-e*sin(phi2)) / (1+e*sin(phi2)), e/2 );

   n = ( log( m1 ) - log( m2 ) ) / ( log( t1 ) - log( t2 ) );
   if( n >= 0.0 )
      sign_n = 1;
   else
      sign_n = -1;
   F = m1 / ( n * pow( t1, n ) );
   rho0 = a * F * pow( t0, n );

   rho = sign_n * sqrt( x*x + pow(rho0-y,2) );
   if( sign_n == 1 )
      theta = atan2( x, rho0-y );
   else
      theta = atan2( -x, y-rho0 );

   t = pow( rho/a/F, 1.0/n );

   chi = 2 * pi_over_4 - 2 * atan( t );

   latitude = chi +
              (e_sqrd/2 + 5*pow(e,4)/24 + pow(e,6)/12 + 13*pow(e,8)/360) *
              sin(2*chi) +
              (7*pow(e,4)/48 + 29*pow(e,6)/240 + 811*pow(e,8)/11520) *
              sin(4*chi) +
              (7*pow(e,6)/120 + 81*pow(e,8)/1120)*sin(6*chi) +
              (4279*pow(e,8)/161280)*sin(8*chi);
   longitude = theta / n + lambda0;

   // convert from radians to degrees
   latitude /= pi_over_180;
   longitude /= pi_over_180;

   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::fwd_transform_lambert_conf_conic( double latitude,
                                                double longitude,
                                                int &hpix, int &vpix )
{
   // performs forward transform for lambert conformal conic projection
   // using data in m_geodata
   // returns pixel coordinates for specified latitude and longitude
   // returns SUCCESS or FAILURE

   int sign_n;
   double dhpix, dvpix;
   double a, b, f, e_sqrd, e, x, y, phi, lambda;
   double phi0, phi1, phi2, lambda0, theta;
   double t, rho, rho0, F, n, m1, m2, t0, t1, t2;
   double pi_over_180 = 4.0 * atan( 1.0 ) / 180.0;
   double pi_over_4 = atan( 1.0 );

   phi = latitude * pi_over_180;
   lambda = longitude * pi_over_180;

   a = m_geodata.m_geog_semi_major_axis;
   b = m_geodata.m_geog_semi_minor_axis;
   f = ( a - b ) / a;
   e_sqrd = 2*f - f*f;
   e = sqrt( e_sqrd );

   // 2SP's origin is ProjFalseOrigin{Lat,Long}; ProjNatOrigin* is the 1SP
   // spelling, which older writers used here.  Take whichever is present -
   // a file that loads today has the same one it always had.
   if( m_geodata.m_proj_false_origin_lat_present )
      phi0 = m_geodata.m_proj_false_origin_lat * pi_over_180;
   else
      phi0 = m_geodata.m_proj_nat_origin_lat * pi_over_180;
   phi1 = m_geodata.m_proj_std_parallel_1 * pi_over_180;
   phi2 = m_geodata.m_proj_std_parallel_2 * pi_over_180;
   if( m_geodata.m_proj_nat_origin_long_present )
      lambda0 = m_geodata.m_proj_nat_origin_long * pi_over_180;
   else
      lambda0 = m_geodata.m_proj_false_origin_long * pi_over_180;

   m1 = cos( phi1 ) / sqrt( 1 - e_sqrd * sin( phi1 ) * sin( phi1 ) );
   m2 = cos( phi2 ) / sqrt( 1 - e_sqrd * sin( phi2 ) * sin( phi2 ) );

   t0 = tan( pi_over_4 - phi0/2 ) /
        pow( (1-e*sin(phi0)) / (1+e*sin(phi0)), e/2 );
   t1 = tan( pi_over_4 - phi1/2 ) /
        pow( (1-e*sin(phi1)) / (1+e*sin(phi1)), e/2 );
   t2 = tan( pi_over_4 - phi2/2 ) /
        pow( (1-e*sin(phi2)) / (1+e*sin(phi2)), e/2 );

   n = ( log( m1 ) - log( m2 ) ) / ( log( t1 ) - log( t2 ) );
   if( n >= 0.0 )
      sign_n = 1;
   else
      sign_n = -1;
   F = m1 / ( n * pow( t1, n ) );
   rho0 = a * F * pow( t0, n );

   t = tan( pi_over_4 - phi/2 ) /
        pow( (1-e*sin(phi)) / (1+e*sin(phi)), e/2 );
   rho = a * F * pow( t, n );
   theta = n * (lambda - lambda0);
   x = rho * sin(theta);
   y = rho0 - rho * cos(theta);

   // add false easting to x and false northing to y - the same 2SP fallback
   // the inverse transform makes
   if( m_geodata.m_proj_false_easting_present ||
       !m_geodata.m_proj_false_origin_easting_present )
      x += m_geodata.m_proj_false_easting;
   else
      x += m_geodata.m_proj_false_origin_easting;
   if( m_geodata.m_proj_false_northing_present ||
       !m_geodata.m_proj_false_origin_northing_present )
      y += m_geodata.m_proj_false_northing;
   else
      y += m_geodata.m_proj_false_origin_northing;

   dhpix = m_geodata.m_model_tie_point_hpix[0] +
           (x - m_geodata.m_model_tie_point_x[0]) /
           m_geodata.m_model_pixel_scale_x;
   dvpix = m_geodata.m_model_tie_point_vpix[0] -
           (y - m_geodata.m_model_tie_point_y[0]) /
           m_geodata.m_model_pixel_scale_y;

   // round to nearest integer
   if( dhpix >= 0.0 )
      hpix = (int)(dhpix + 0.5);
   else
      hpix = (int)(dhpix - 0.5);
   if( dvpix >= 0.0 )
      vpix = (int)(dvpix + 0.5);
   else
      vpix = (int)(dvpix - 0.5);

   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::inv_transform_albers_equal_area( int hpix, int vpix,
                                        double &latitude, double &longitude )
{
   // performs inverse transform for albers equal area projection
   // using data in m_geodata
   // returns latitude and longitude in degrees for specified pixel
   // returns SUCCESS or FAILURE

   int sign_n;
   double a, b, f, e_sqrd, e, x, y;
   double phi0, phi1, phi2, lambda0, theta, beta;
   double rho, rho0, n, m1, m2;
   double q, q0, q1, q2, c;
   double pi_over_180 = 4.0 * atan( 1.0 ) / 180.0;
//   double pi_over_4 = atan( 1.0 );

   x = m_geodata.m_model_tie_point_x[0] + m_geodata.m_model_pixel_scale_x *
       ( (double)hpix - m_geodata.m_model_tie_point_hpix[0] );
   y = m_geodata.m_model_tie_point_y[0] - m_geodata.m_model_pixel_scale_y *
       ( (double)vpix - m_geodata.m_model_tie_point_vpix[0] );

   // subtract false easting from x and false northing from y
   x -= m_geodata.m_proj_false_easting;
   y -= m_geodata.m_proj_false_northing;

   a = m_geodata.m_geog_semi_major_axis;
   b = m_geodata.m_geog_semi_minor_axis;
   f = ( a - b ) / a;
   e_sqrd = 2*f - f*f;
   e = sqrt( e_sqrd );

   phi0 = m_geodata.m_proj_nat_origin_lat * pi_over_180;
   phi1 = m_geodata.m_proj_std_parallel_1 * pi_over_180;
   phi2 = m_geodata.m_proj_std_parallel_2 * pi_over_180;
   if (m_geodata.m_proj_center_long_present)
		lambda0 = m_geodata.m_proj_center_long * pi_over_180;
   else if (m_geodata.m_proj_nat_origin_long_present)
		lambda0 = m_geodata.m_proj_nat_origin_long * pi_over_180;
   else
	   return FAILURE;


   m1 = cos( phi1 ) / sqrt( 1 - e_sqrd * sin( phi1 ) * sin( phi1 ) );
   m2 = cos( phi2 ) / sqrt( 1 - e_sqrd * sin( phi2 ) * sin( phi2 ) );

   q0 = (1-e_sqrd) * (
                        sin(phi0)/(1-e_sqrd*sin(phi0)*sin(phi0)) -
                        (1./2./e)*log( (1-e*sin(phi0))/(1+e*sin(phi0)) )
                     );
   q1 = (1-e_sqrd) * (
                        sin(phi1)/(1-e_sqrd*sin(phi1)*sin(phi1)) -
                        (1./2./e)*log( (1-e*sin(phi1))/(1+e*sin(phi1)) )
                     );
   q2 = (1-e_sqrd) * (
                        sin(phi2)/(1-e_sqrd*sin(phi2)*sin(phi2)) -
                        (1./2./e)*log( (1-e*sin(phi2))/(1+e*sin(phi2)) )
                     );

   n = ( m1*m1 - m2*m2 ) / ( q2 - q1 );
   if( n >= 0.0 )
      sign_n = 1;
   else
      sign_n = -1;

   c = m1*m1 + n*q1;
   rho0 = a * sqrt(c-n*q0) / n;

   rho = sqrt( x*x + pow(rho0-y,2) );
   q = ( c - rho*rho*n*n/a/a ) / n;
   if( sign_n == 1 )
      theta = atan2( x, rho0-y );
   else
      theta = atan2( -x, y-rho0 );

   beta = asin( q / ( 1 - ((1-e_sqrd)/2/e) * log( (1-e)/(1+e) ) ) );

   latitude = beta +
              ( e_sqrd/3 + 31*pow(e,4)/180 + 517*pow(e,6)/5040 ) * sin(2*beta) +
              ( 23*pow(e,4)/360 + 251*pow(e,6)/3780 ) * sin(4*beta) +
              ( 761*pow(e,6)/45360 ) * sin(6*beta);
   longitude = lambda0 + theta/n;

   // convert from radians to degrees
   latitude /= pi_over_180;
   longitude /= pi_over_180;

   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::fwd_transform_albers_equal_area( double latitude,
                                               double longitude,
                                               int &hpix, int &vpix )
{
   // performs forard transform for albers equal area projection
   // using data in m_geodata
   // returns pixel coordinates for specified latitude and longitude
   // returns SUCCESS or FAILURE

   int sign_n;
   double dhpix, dvpix;
   double a, b, f, e_sqrd, e, x, y;
   double phi, lambda;
   double phi0, phi1, phi2, lambda0, theta;
   double rho, rho0, n, m1, m2;
   double q, q0, q1, q2, c;
   double pi_over_180 = 4.0 * atan( 1.0 ) / 180.0;
//   double pi_over_4 = atan( 1.0 );

   phi = latitude * pi_over_180;
   lambda = longitude * pi_over_180;

   a = m_geodata.m_geog_semi_major_axis;
   b = m_geodata.m_geog_semi_minor_axis;
   f = ( a - b ) / a;
   e_sqrd = 2*f - f*f;
   e = sqrt( e_sqrd );

   phi0 = m_geodata.m_proj_nat_origin_lat * pi_over_180;
   phi1 = m_geodata.m_proj_std_parallel_1 * pi_over_180;
   phi2 = m_geodata.m_proj_std_parallel_2 * pi_over_180;
   if (m_geodata.m_proj_center_long_present)
		lambda0 = m_geodata.m_proj_center_long * pi_over_180;
   else if (m_geodata.m_proj_nat_origin_long_present)
		lambda0 = m_geodata.m_proj_nat_origin_long * pi_over_180;
   else
	   return FAILURE;

   m1 = cos( phi1 ) / sqrt( 1 - e_sqrd * sin( phi1 ) * sin( phi1 ) );
   m2 = cos( phi2 ) / sqrt( 1 - e_sqrd * sin( phi2 ) * sin( phi2 ) );

   q0 = (1-e_sqrd) * (
                        sin(phi0)/(1-e_sqrd*sin(phi0)*sin(phi0)) -
                        (1./2./e)*log( (1-e*sin(phi0))/(1+e*sin(phi0)) )
                     );
   q1 = (1-e_sqrd) * (
                        sin(phi1)/(1-e_sqrd*sin(phi1)*sin(phi1)) -
                        (1./2./e)*log( (1-e*sin(phi1))/(1+e*sin(phi1)) )
                     );
   q2 = (1-e_sqrd) * (
                        sin(phi2)/(1-e_sqrd*sin(phi2)*sin(phi2)) -
                        (1./2./e)*log( (1-e*sin(phi2))/(1+e*sin(phi2)) )
                     );

   n = ( m1*m1 - m2*m2 ) / ( q2 - q1 );
   if( n >= 0.0 )
      sign_n = 1;
   else
      sign_n = -1;

   c = m1*m1 + n*q1;
   rho0 = a * sqrt(c-n*q0) / n;

   q = (1-e_sqrd) * ( sin(phi)/(1-e_sqrd*sin(phi)*sin(phi)) -
                      (1./2./e) * log( (1-e*sin(phi))/(1+e*sin(phi)) ) );
   rho = a * sqrt(c-n*q) / n;
   theta = n*(lambda-lambda0);

   x = rho * sin(theta);
   y = rho0 - rho*cos(theta);

   // add false easting to x and false northing to y
   x += m_geodata.m_proj_false_easting;
   y += m_geodata.m_proj_false_northing;

   dhpix = m_geodata.m_model_tie_point_hpix[0] +
           (x - m_geodata.m_model_tie_point_x[0]) /
           m_geodata.m_model_pixel_scale_x;
   dvpix = m_geodata.m_model_tie_point_vpix[0] -
           (y - m_geodata.m_model_tie_point_y[0]) /
           m_geodata.m_model_pixel_scale_y;

   // round to nearest integer
   if( dhpix >= 0.0 )
      hpix = (int)(dhpix + 0.5);
   else
      hpix = (int)(dhpix - 0.5);
   if( dvpix >= 0.0 )
      vpix = (int)(dvpix + 0.5);
   else
      vpix = (int)(dvpix - 0.5);

   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_image_size( int &image_width, int &image_length )
{
   // this function returns the image width and length in the arguments
   // returns SUCCESS or FAILURE

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
	   return FAILURE;

   image_width = m_image_width;
   image_length = m_image_length;

   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_subsampled_subimage_size( int sampling,
      int min_hpix, int min_vpix, int max_hpix, int max_vpix,
      int &image_width, int &image_length )
{
   // this function returns the subsampled image width and length in the
   // arguments for the subimage specified by min_hpix, min_vpix, max_hpix,
   // max_vpix. the sampling argument specifies the rows and columns that are
   // retained. for example, a sampling value of 3 specifies that every 3rd row
   // and column should be retained in the subsampled image.
   // returns SUCCESS or FAILURE

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error ) return FAILURE;

   // check for valid sampling
   if( sampling < 1 )
	   return FAILURE;

   // return failure if subimage is invalid
   if( min_hpix < 0 || min_hpix >= (int)m_image_width || min_hpix > max_hpix )
      return FAILURE;
   if( max_hpix < 0 || max_hpix >= (int)m_image_width )
	   return FAILURE;
   if( min_vpix < 0 || min_vpix >= (int)m_image_length || min_vpix > max_vpix )
      return FAILURE;
   if( max_vpix < 0 || max_vpix >= (int)m_image_length )
	   return FAILURE;

   image_width = (max_hpix-min_hpix) / sampling + 1;
   image_length = (max_vpix-min_vpix) / sampling + 1;

   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_rgb_subimage( int min_hpix, int min_vpix, int max_hpix,
                                int max_vpix, unsigned char *red_array,
                                unsigned char *green_array,
                                unsigned char *blue_array )
{
   // this function returns the pixel colors in the argument arrays for the
   // specified subimage
   // returns SUCCESS or FAILURE

   int min_tile_row, min_tile_col, max_tile_row, max_tile_col;
   int tile_row, tile_col, tile, rslt;
   int image_hpix, image_vpix;
   int subimage_hpix, subimage_vpix, subimage_width, subimage_index;
   int tile_hpix, tile_vpix, tile_index;

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
	   return FAILURE;

   // return error if image type is not supported
   if( !m_image_type_supported )
	   return FAILURE;

   // return failure if subimage is invalid
   if( min_hpix < 0 || min_hpix >= (int)m_image_width || min_hpix > max_hpix )
      return FAILURE;
   if( max_hpix < 0 || max_hpix >= (int)m_image_width )
	   return FAILURE;
   if( min_vpix < 0 || min_vpix >= (int)m_image_length || min_vpix > max_vpix )
      return FAILURE;
   if( max_vpix < 0 || max_vpix >= (int)m_image_length )
	   return FAILURE;

   subimage_width = max_hpix - min_hpix + 1;

   // check for tiled image
   if( m_image_is_tiled )
   {
      // image is tiled
      // determine range of tile rows and columns in subimage
      min_tile_row = min_vpix / m_tile_length;
      max_tile_row = max_vpix / m_tile_length;
      min_tile_col = min_hpix / m_tile_width;
      max_tile_col = max_hpix / m_tile_width;

      // loop through tiles covering the subimage
      for( tile_row = min_tile_row; tile_row <= max_tile_row; tile_row++ )
      {
         for( tile_col = min_tile_col; tile_col <= max_tile_col; tile_col++ )
         {
            tile = m_num_tiles_across * tile_row + tile_col;

            if( get_tile_as_rgb_image( tile, m_tile_red_array, m_tile_green_array,
               m_tile_blue_array ) != SUCCESS )
               goto FAIL;

            // copy data from tile
            for( tile_vpix = 0; tile_vpix < (int)m_tile_length; tile_vpix++ )
            {
               image_vpix = tile_row * m_tile_length + tile_vpix;
               if( image_vpix < min_vpix )
                  continue;
               if( image_vpix > max_vpix )
                  break;
               for( tile_hpix = 0; tile_hpix < (int)m_tile_width; tile_hpix++ )
               {
                  image_hpix = tile_col * m_tile_width + tile_hpix;
                  if( image_hpix < min_hpix )
                     continue;
                  if( image_hpix > max_hpix )
                     break;
                  tile_index = tile_vpix * m_tile_width + tile_hpix;
                  subimage_hpix = image_hpix - min_hpix;
                  subimage_vpix = image_vpix - min_vpix;
                  subimage_index = subimage_vpix * subimage_width + subimage_hpix;
                  red_array[subimage_index] = m_tile_red_array[tile_index];
                  green_array[subimage_index] = m_tile_green_array[tile_index];
                  blue_array[subimage_index] = m_tile_blue_array[tile_index];
                  tile_index++;
               }
            }
         }
      }
   }
   else
   {
      // image is not tiled
      // select image type and call appropriate function
      switch( m_image_type )
      {
         case GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED:
            goto FAIL;

         case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
			 if (m_samples_per_pixel == 3)
				 rslt = get_24bit_rgb_as_rgb_subimage( min_hpix, min_vpix, max_hpix,
							 max_vpix, red_array, green_array, blue_array );
			 else
				rslt =  get_8bit_grayscale_as_rgb_subimage( min_hpix, min_vpix, max_hpix,
                max_vpix, red_array, green_array, blue_array );
			 if (rslt != SUCCESS )
               goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE:
            if( get_16bit_grayscale_as_rgb_subimage( min_hpix, min_vpix, max_hpix,
                max_vpix, red_array, green_array, blue_array ) != SUCCESS )
               goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_256_COLOR:
            if( get_8bit_palette_as_rgb_subimage( min_hpix, min_vpix, max_hpix,
                max_vpix, red_array, green_array, blue_array ) != SUCCESS )
               goto FAIL;
            break;

         case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
            if( get_24bit_rgb_as_rgb_subimage( min_hpix, min_vpix, max_hpix,
                max_vpix, red_array, green_array, blue_array ) != SUCCESS )
               goto FAIL;
            break;

         default:                           // should always be one of above
            goto FAIL;
      }
   }

   return SUCCESS;

FAIL:
   return FAILURE;
}

// *****************************************************************
// ******************************************************************************

int CGeoTiffFrameFile::get_subsampled_rgb_subimage( int sampling,
                         int min_hpix, int min_vpix, int max_hpix, int max_vpix,
                         unsigned char *red_array, unsigned char *green_array,
                         unsigned char *blue_array )
{
   // this function returns the pixel colors in the argument arrays for the
   // specified subimage, subsampled according to the sampling argument
   // the sampling argument indicates the fraction of the image's rows and
   // columns that are to be retained in the subsampled image. a sampling of 3
   // indicates that every third row and column should be retained
   // returns SUCCESS or FAILURE

   int row, col, index, icol;
   unsigned char *red_row = NULL, *green_row = NULL, *blue_row = NULL;
   int min_tile_row, min_tile_col, max_tile_row, max_tile_col;
   int tile_row, tile_col, tile;
   int image_hpix, image_vpix;
   int subimage_hpix, subimage_vpix, subimage_width, subimage_index;
   int subsampled_subimage_hpix, subsampled_subimage_vpix;
   int subsampled_subimage_width, subsampled_subimage_index;
   int tile_hpix, tile_vpix, tile_index;
	int width, height;

	width = max_hpix - min_hpix + 1;
	height = max_vpix - min_vpix + 1;

   // return if no file loaded or there was an error loading the tiff file
   if( !m_file_loaded || m_error )
	   return FAILURE;

   // return error if image type is not supported
   if( !m_image_type_supported )
	   return FAILURE;

   // return failure if subimage is invalid
   if( min_hpix < 0 || min_hpix >= (int)m_image_width || min_hpix > max_hpix )
      return FAILURE;
   if( max_hpix < 0 || max_hpix >= (int)m_image_width )
	   return FAILURE;
   if( min_vpix < 0 || min_vpix >= (int)m_image_length || min_vpix > max_vpix )
      return FAILURE;
   if( max_vpix < 0 || max_vpix >= (int)m_image_length )
	   return FAILURE;

   // return FAILURE if sampling is less than 1
   if( sampling < 1 )
	   return FAILURE;

   subimage_width = max_hpix - min_hpix + 1;
   subsampled_subimage_width = (subimage_width-1) / sampling + 1;

   // check for tiled image
   if( m_image_is_tiled )
   {
      // image is tiled

      // determine range of tile rows and columns in subimage
      min_tile_row = min_vpix / m_tile_length;
      max_tile_row = max_vpix / m_tile_length;
      min_tile_col = min_hpix / m_tile_width;
      max_tile_col = max_hpix / m_tile_width;

      // loop through tiles covering the subimage
      for( tile_row = min_tile_row; tile_row <= max_tile_row; tile_row++ )
      {
         for( tile_col = min_tile_col; tile_col <= max_tile_col; tile_col++ )
         {
            tile = m_num_tiles_across * tile_row + tile_col;

            if( get_tile_as_rgb_image( tile, m_tile_red_array, m_tile_green_array,
               m_tile_blue_array ) != SUCCESS )
               goto FAIL;

            // copy data from tile
            for( tile_vpix = 0; tile_vpix < (int)m_tile_length; tile_vpix++ )
            {
               image_vpix = tile_row * m_tile_length + tile_vpix;
               if( image_vpix < min_vpix )
                  continue;
               if( image_vpix > max_vpix )
                  break;
               for( tile_hpix = 0; tile_hpix < (int)m_tile_width; tile_hpix++ )
               {
                  image_hpix = tile_col * m_tile_width + tile_hpix;
                  if( image_hpix < min_hpix )
                     continue;
                  if( image_hpix > max_hpix )
                     break;
                  tile_index = tile_vpix * m_tile_width + tile_hpix;
                  subimage_hpix = image_hpix - min_hpix;
                  subimage_vpix = image_vpix - min_vpix;
                  subimage_index = subimage_vpix * subimage_width + subimage_hpix;
                  if( subimage_hpix % sampling == 0 && subimage_vpix % sampling == 0 )
                  {
                     subsampled_subimage_hpix = subimage_hpix / sampling;
                     subsampled_subimage_vpix = subimage_vpix / sampling;
                     subsampled_subimage_index =
                        subsampled_subimage_vpix * subsampled_subimage_width +
                        subsampled_subimage_hpix;
                     red_array[subsampled_subimage_index] =
                        m_tile_red_array[tile_index];
                     green_array[subsampled_subimage_index] =
                        m_tile_green_array[tile_index];
                     blue_array[subsampled_subimage_index] =
                        m_tile_blue_array[tile_index];
                  }
               }
            }
         }
      }
   }
   else
   {
      // allocate arrays to hold one row of data

      red_row = new unsigned char[subimage_width];
      if( red_row == NULL )
		  goto FAIL;

      green_row = new unsigned char[subimage_width];
      if( green_row == NULL )
		  goto FAIL;

      blue_row = new unsigned char[subimage_width];
      if( blue_row == NULL )
		  goto FAIL;

      // read the subsampled rows, then subsample the columns
      index = 0;
      for( row = min_vpix; row <= max_vpix; row += sampling )
      {
         // read the row
         if( get_rgb_subimage( min_hpix, row, max_hpix, row,
             red_row, green_row, blue_row ) != SUCCESS )
            goto FAIL;
         for( col = min_hpix; col <= max_hpix; col += sampling )
         {
            icol = col - min_hpix;
            red_array[index] = red_row[icol];
            green_array[index] = green_row[icol];
            blue_array[index] = blue_row[icol];
            index++;
         }
      }
      // deallocate row memory
      delete [] red_row;
      delete [] green_row;
      delete [] blue_row;
   }

   return SUCCESS;

FAIL:
   // deallocate memory
   if ( red_row != NULL )
	   delete [] red_row;
   if ( green_row != NULL )
	   delete [] green_row;
   if ( blue_row != NULL )
	   delete [] blue_row;
   return FAILURE;
}
// end of get_subsampled_rgb_subimage

// private interface

int CGeoTiffFrameFile::read_directory( )
{
   // this function reads the image file directory pointed
   // to by m_directory_offset, including all tags.
   // returns SUCCESS or FAILURE

   int i;
   unsigned short num_tags;
   const int STRING_LEN = 50;
   char string[STRING_LEN];

   // move file pointer to directory offset
   if( fseek( m_file_ptr, m_directory_offset, SEEK_SET ) != 0 )
   {
      m_new_error_description = "Tag directory offset is invalid.";
      return FAILURE;
   }

   // read number of entries
   if( read_unsigned_short( num_tags ) != SUCCESS )
   {
      m_new_error_description = "Error reading number of tag entries.";
      return FAILURE;
   }
   m_num_tags = num_tags;

   // allocate memory for tags
   m_tags = new CTiffTag[m_num_tags];
   if (m_tags == nullptr)
      return FAILURE;

   // read each tag
   for( i = 0; i < m_num_tags; i++ )
      if( read_tag( m_tags[i] ) != SUCCESS )
      {
         sprintf_s( string, STRING_LEN, "Error reading tag %i.", i );
         m_new_error_description = string;
         return FAILURE;
      }

   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::read_tag( CTiffTag &tag )
{
   // this function reads a tiff tag into the argument
   // from the current file position
   // returns SUCCESS or FAILURE

   int i, data_size_in_bytes, file_position;
   double ratio;

   // clear the tag
   tag.clear( );

   // read tag number
   if( read_unsigned_short( tag.m_tag_id ) != SUCCESS )
	   goto FAIL;

   // read data type
   if( read_unsigned_short( tag.m_type ) != SUCCESS )
	   goto FAIL;

   // read data count
   if( read_unsigned_int( tag.m_count ) != SUCCESS )
	   goto FAIL;

   // save file position
   file_position = ftell( m_file_ptr );

   // allocate memory for data and determine data size
   switch( tag.m_type )
   {
      case GEOTIFF_BYTE:              // 8-bit unsigned integer
         data_size_in_bytes = 1;
         tag.m_byte_values = new unsigned char[tag.m_count];
         if (tag.m_byte_values == nullptr)
            goto FAIL;
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
				goto FAIL;
            // seek value offset position in file
            if( fseek( m_file_ptr, tag.m_value_offset, SEEK_SET ) != 0 )
               goto FAIL;
         }
         // read value(s) from current file location
         if( fread( tag.m_byte_values, 1, tag.m_count, m_file_ptr ) != tag.m_count )
			 goto FAIL;
         break;
      case GEOTIFF_ASCII:             // null-terminated string
         data_size_in_bytes = 1;
         tag.m_ascii_values = new char[tag.m_count];
         if (tag.m_ascii_values == nullptr)
            goto FAIL;
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
				goto FAIL;
            // seek value offset position in file
            if( fseek( m_file_ptr, tag.m_value_offset, SEEK_SET ) != 0 )
               goto FAIL;
         }
         // read value(s) from current file location
         if( read_string( tag.m_ascii_values, tag.m_count ) !=  SUCCESS)
            goto FAIL;
         break;
      case GEOTIFF_SHORT:             // unsigned short (16-bit) integer
         data_size_in_bytes = 2;
         tag.m_short_values = new unsigned short[tag.m_count];
         if (tag.m_short_values == nullptr)
            goto FAIL;
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
				goto FAIL;
            // seek value offset position in file
            if( fseek( m_file_ptr, tag.m_value_offset, SEEK_SET ) != 0 )
               goto FAIL;
         }
         // read value(s) from current file location
         for( i = 0; i < (int)tag.m_count; i++ )
            if( read_unsigned_short( tag.m_short_values[i] ) !=  SUCCESS)
               goto FAIL;
        break;
      case GEOTIFF_LONG:              // unsigned long (32-bit) integer
         data_size_in_bytes = 4;
         tag.m_long_values = new unsigned int[tag.m_count];
         if (tag.m_long_values == nullptr)
            goto FAIL;
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
				goto FAIL;
            // seek value offset position in file
            if( fseek( m_file_ptr, tag.m_value_offset, SEEK_SET ) != 0 )
               goto FAIL;
         }
         // read value(s) from current file location
         for( i = 0; i < (int)tag.m_count; i++ )
            if( read_unsigned_int( tag.m_long_values[i] ) !=  SUCCESS)
               goto FAIL;
         break;
      case GEOTIFF_RATIONAL:          // num/denom pair of unsigned longs
         data_size_in_bytes = 8;
         tag.m_rational_numerator_values = new unsigned int[tag.m_count];
         if (tag.m_rational_numerator_values == nullptr)
            goto FAIL;
         tag.m_rational_denominator_values = new unsigned int[tag.m_count];
         if (tag.m_rational_denominator_values == nullptr)
            goto FAIL;

         // store ratio as a double
         tag.m_double_values = new double[tag.m_count];
         if (tag.m_double_values == nullptr)
            goto FAIL;
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
				goto FAIL;
            // seek value offset position in file
            if( fseek( m_file_ptr, tag.m_value_offset, SEEK_SET ) != 0 )
               goto FAIL;
         }
         // read value(s) from current file location
         for( i = 0; i < (int)tag.m_count; i++ )
         {
            if( read_unsigned_int( tag.m_rational_numerator_values[i] ) != SUCCESS)
				goto FAIL;
            if( read_unsigned_int( tag.m_rational_denominator_values[i] ) != SUCCESS)
				goto FAIL;
            // calculate and store ratio as a double, checking for division by
            // zero
            if( tag.m_rational_denominator_values[i] != 0 )
               ratio = tag.m_rational_numerator_values[i] /
                       tag.m_rational_denominator_values[i];
            else
               ratio = HUGE_VAL;
            tag.m_double_values[i] = ratio;
         }
         break;
      case GEOTIFF_SBYTE:             // 8-bit signed integer
         data_size_in_bytes = 1;
         tag.m_sbyte_values = new char[tag.m_count];
         if (tag.m_sbyte_values == nullptr)
            goto FAIL;
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
				goto FAIL;
            // seek value offset position in file
            if( fseek( m_file_ptr, tag.m_value_offset, SEEK_SET ) != 0 )
               goto FAIL;
         }
         // read value(s) from current file location
         if( fread( tag.m_sbyte_values, 1, tag.m_count, m_file_ptr ) != tag.m_count )
			 goto FAIL;
         break;
      case GEOTIFF_UNDEFINED_TYPE:         // 8-bit undefined data type
         data_size_in_bytes = 1;
         tag.m_undefined_values = new char[tag.m_count];
         if (tag.m_undefined_values == nullptr)
            goto FAIL;
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
				goto FAIL;
            // seek value offset position in file
            if( fseek( m_file_ptr, tag.m_value_offset, SEEK_SET ) != 0 )
               goto FAIL;
         }
         // read value(s) from current file location
         if( fread( tag.m_undefined_values, 1, tag.m_count, m_file_ptr ) != tag.m_count )
			 goto FAIL;
         break;
      case GEOTIFF_SSHORT:            // signed short (16-bit) integer
         data_size_in_bytes = 2;
         tag.m_sshort_values = new short[tag.m_count];
         if (tag.m_sshort_values == nullptr)
            goto FAIL;
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
				goto FAIL;
            // seek value offset position in file
            if( fseek( m_file_ptr, tag.m_value_offset, SEEK_SET ) != 0 )
               goto FAIL;
         }
         // read value(s) from current file location
         for( i = 0; i < (int)tag.m_count; i++ )
            if( read_signed_short( tag.m_sshort_values[i] ) !=  SUCCESS)
               goto FAIL;
         break;
      case GEOTIFF_SLONG:             // signed long (32-bit) integer
         data_size_in_bytes = 4;
         tag.m_slong_values = new int[tag.m_count];
         if (tag.m_slong_values == nullptr)
            goto FAIL;
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
				goto FAIL;
            // seek value offset position in file
            if( fseek( m_file_ptr, tag.m_value_offset, SEEK_SET ) != 0 )
               goto FAIL;
         }
         // read value(s) from current file location
         for( i = 0; i < (int)tag.m_count; i++ )
            if( read_signed_int( tag.m_slong_values[i] ) !=  SUCCESS)
				goto FAIL;
         break;
      case GEOTIFF_SRATIONAL:         // num/denom pair of signed longs
         data_size_in_bytes = 8;
         tag.m_srational_numerator_values = new int[tag.m_count];
         if (tag.m_srational_numerator_values == nullptr)
            goto FAIL;
         tag.m_srational_denominator_values = new int[tag.m_count];
         if (tag.m_srational_denominator_values == nullptr)
            goto FAIL;
         tag.m_double_values = new double[tag.m_count];
         if (tag.m_double_values == nullptr)
            goto FAIL;
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
				goto FAIL;
            // seek value offset position in file
            if( fseek( m_file_ptr, tag.m_value_offset, SEEK_SET ) != 0 )
               goto FAIL;
         }
         // read value(s) from current file location
         for( i = 0; i < (int)tag.m_count; i++ )
         {
            if( read_signed_int( tag.m_srational_numerator_values[i] ) != SUCCESS)
				goto FAIL;
            if( read_signed_int( tag.m_srational_denominator_values[i] ) != SUCCESS)
				goto FAIL;
            // calculate and store ratio as a double, checking for division by
            // zero
            if( tag.m_srational_denominator_values[i] != 0 )
               ratio = tag.m_srational_numerator_values[i] /
                       tag.m_srational_denominator_values[i];
            else
            {
               if( tag.m_srational_numerator_values[i] >= 0 )
                  ratio = HUGE_VAL;
               else
                  ratio = -HUGE_VAL;
            }
            tag.m_double_values[i] = ratio;
         }
         break;
      case GEOTIFF_FLOAT:             // 4-byte float value
         data_size_in_bytes = 4;
         tag.m_float_values = new float[tag.m_count];
         if (tag.m_float_values == nullptr)
            goto FAIL;
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
				goto FAIL;
            // seek value offset position in file
            if( fseek( m_file_ptr, tag.m_value_offset, SEEK_SET ) != 0 )
               goto FAIL;
         }
         // read value(s) from current file location
         for( i = 0; i < (int)tag.m_count; i++ )
            if( read_float( tag.m_float_values[i] ) !=  SUCCESS )
				goto FAIL;
         break;
      case GEOTIFF_DOUBLE:            // 8-byte double value
         data_size_in_bytes = 8;
         tag.m_double_values = new double[tag.m_count];
         if (tag.m_double_values == nullptr)
            goto FAIL;
         if( data_size_in_bytes * tag.m_count > 4 )
         {
            // read value offset
            if( read_unsigned_int( tag.m_value_offset ) != SUCCESS )
				goto FAIL;
            // seek value offset position in file
            if( fseek( m_file_ptr, tag.m_value_offset, SEEK_SET ) != 0 )
               goto FAIL;
         }
         // read value(s) from current file location
         for( i = 0; i < (int)tag.m_count; i++ )
            if( read_double( tag.m_double_values[i] ) !=  SUCCESS )
				goto FAIL;
         break;
      default:
         // data type is unknown - don't read values
         break;
   }

   // restore file position + 4 bytes
   file_position += 4;
   if( fseek( m_file_ptr, file_position, SEEK_SET ) != 0 )
	   goto FAIL;

   tag.m_tag_name = get_tag_name( tag.m_tag_id );
   tag.m_type_name = get_type_name( tag.m_type );
   tag.m_value_name = get_tag_value_name( tag );
   tag.m_defined = TRUE;

   // return SUCCESS
   return SUCCESS;

FAIL:
   // on failure, clear the tag and return FAILURE
   tag.clear( );
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::find_tag( unsigned short tag_id, int &index )
{
   // finds the index of the specified tag_id in the tag array
   // returns SUCCESS or FAILURE

   for( index = 0; index < m_num_tags; index++ )
      if( m_tags[index].m_tag_id == tag_id )
		  return SUCCESS;

   index = -1;
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::inspect_tags( )
{
   // this function inspects the tags for required data and image information
   // returns SUCCESS or FAILURE depending on whether or not the required
   // data is present in the tags

   int i, index, tag_index, count;

   // get image width (no default)
   if( find_tag( GEOTIFF_IMAGE_WIDTH_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_image_width = m_tags[tag_index].m_short_values[0];
            break;
         case GEOTIFF_LONG:
            m_image_width = m_tags[tag_index].m_long_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "ImageWidth tag is invalid data type.";
            return FAILURE;
      }
   }
   else
   {
      m_new_error_description = "ImageWidth tag is missing.";
      return FAILURE;
   }
   if( m_image_width == 0 )
   {
      m_new_error_description = "Image width is zero.";
      return FAILURE;
   }

   // get image length (no default)
   if( find_tag( GEOTIFF_IMAGE_LENGTH_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_image_length = m_tags[tag_index].m_short_values[0];
            break;
         case GEOTIFF_LONG:
            m_image_length = m_tags[tag_index].m_long_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "ImageLength tag is invalid data type.";
            return FAILURE;
      }
   }
   else
   {
      m_new_error_description = "ImageLength tag is missing.";
      return FAILURE;
   }
   if( m_image_length == 0 )
   {
      m_new_error_description = "Image length is zero.";
      return FAILURE;
   }

   // calculate number of pixels
   m_num_pixels = m_image_width * m_image_length;

   // get compression scheme (default = no compression)
   if( find_tag( GEOTIFF_COMPRESSION_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_compression_scheme = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "Compression tag is invalid data type.";
            return FAILURE;
      }
      switch( m_compression_scheme )
      {
         case GEOTIFF_NO_COMPRESSION:
         case GEOTIFF_CCITT_1D:
         case GEOTIFF_PACKBITS:
         case GEOTIFF_LZW:
		 case GEOTIFF_JPEG:
            break;
         default:                      // all others unsupported at present
            m_new_error_description = "Compression tag value is not supported.";
            return FAILURE;
      }
   }
   else
      m_compression_scheme = GEOTIFF_NO_COMPRESSION;

   // get the predictor (default is 1, none).  Only 1 and 2 exist for integer
   // samples; 3 is the floating-point predictor, which no reader here wants.
   if( find_tag( GEOTIFF_PREDICTOR_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_predictor = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "Predictor tag is invalid data type.";
            return FAILURE;
      }
      switch( m_predictor )
      {
         case 1:                        // no predictor
            break;
         case 2:                        // horizontal differencing
            break;
         default:
            m_new_error_description = "Predictor tag value is not supported.";
            return FAILURE;
      }
   }
   else
      m_predictor = 1;

   // get the jpeg table info if it's there
   if (m_compression_scheme == GEOTIFF_JPEG)
   {
	   if( find_tag( GEOTIFF_JPEG_TABLES_TAG, tag_index ) == SUCCESS )
	   {
		   int k, cnt;

		   cnt = m_jpeg_table_len = m_tags[tag_index].m_count;
		   for (k=0; k<cnt; k++)
			   m_jpeg_table[k] = m_tags[tag_index].m_undefined_values[k];

	   }
   }

   // get photometric interpretation (no default)
   if( find_tag( GEOTIFF_PHOTOMETRIC_INTERPRETATION_TAG, tag_index ) ==
       SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_photometric_interpretation = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description =
               "PhotometricInterpretation tag is invalid data type.";
            return FAILURE;
      }
      switch( m_photometric_interpretation )
      {
         case GEOTIFF_WHITE_IS_ZERO:
         case GEOTIFF_BLACK_IS_ZERO:
         case GEOTIFF_RGB:
         case GEOTIFF_RGB_PALETTE:
         case GEOTIFF_YCBCR:
            break;
         default:                       // all others not supported at present
            m_new_error_description =
               "PhotometricInterpretation tag value is not supported.";
            return FAILURE;
      }
   }
   else
   {
      m_new_error_description =
         "PhotometricInterpretation tag method is missing.";
      return FAILURE;
   }

   // determine if image is tiled
   if( find_tag( GEOTIFF_TILE_WIDTH_TAG, tag_index ) == SUCCESS )
   {
      m_image_is_tiled = TRUE;
      // get tile width (same for all tiles)
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_tile_width = m_tags[tag_index].m_short_values[0];
            break;
         case GEOTIFF_LONG:
            m_tile_width = m_tags[tag_index].m_long_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "TileWidth tag is invalid data type.";
            return FAILURE;
      }
      // tile width must be an even multiple of 16
      if( m_tile_width < 16 || (m_tile_width % 16) != 0 )
      {
         m_new_error_description = "Tile width is not a multiple of 16.";
         return FAILURE;
      }

      // get tile length (same for all tiles)
      if( find_tag( GEOTIFF_TILE_LENGTH_TAG, tag_index ) == SUCCESS )
      {
         switch( m_tags[tag_index].m_type )
         {
            case GEOTIFF_SHORT:
               m_tile_length = m_tags[tag_index].m_short_values[0];
               break;
            case GEOTIFF_LONG:
               m_tile_length = m_tags[tag_index].m_long_values[0];
               break;
            default:
               // data is wrong type
               m_new_error_description = "TileLength tag is invalid data type.";
               return FAILURE;
         }
         // tile length must be an even multiple of 16
         if( m_tile_length < 16 || (m_tile_length % 16) != 0 )
         {
            m_new_error_description = "Tile length is not a multiple of 16.";
            return FAILURE;
         }
      }
      else
      {
         m_new_error_description = "TileLength tag is missing.";
         return FAILURE;
      }

      // verify that tile offsets are present
      if( find_tag( GEOTIFF_TILE_OFFSETS_TAG, tag_index ) != SUCCESS )
      {
         m_new_error_description = "TileOffsets tag is missing.";
         return FAILURE;
      }

      // verify that tile byte counts are present
      if( find_tag( GEOTIFF_TILE_BYTE_COUNTS_TAG, tag_index ) != SUCCESS )
      {
         m_new_error_description = "TileByteCounts tag is missing.";
         return FAILURE;
      }

      m_num_tile_pixels = m_tile_width * m_tile_length;
      m_num_tiles_across = (m_image_width + m_tile_width - 1) / m_tile_width;
      m_num_tiles_down = (m_image_length + m_tile_length - 1) / m_tile_length;
      m_num_tiles = m_num_tiles_across * m_num_tiles_down;
      // allocate memory to store a tile
       m_tile_red_array = new unsigned char[m_num_tile_pixels];
      if( m_tile_red_array == NULL )
         return FAILURE;
       m_tile_green_array = new unsigned char[m_num_tile_pixels];
      if( m_tile_green_array == NULL )
         return FAILURE;
       m_tile_blue_array = new unsigned char[m_num_tile_pixels];
      if( m_tile_blue_array == NULL )
         return FAILURE;
       m_tile_color_indices = new unsigned char[m_num_tile_pixels];
      if( m_tile_color_indices == NULL )
         return FAILURE;
   }
   else
   {
      // image is stripped, not tiled
      m_image_is_tiled = FALSE;
      // get rows per strip (default is 2^32-1 = 4,294,967,295)
      if( find_tag( GEOTIFF_ROWS_PER_STRIP_TAG, tag_index ) == SUCCESS )
      {
         switch( m_tags[tag_index].m_type )
         {
            case GEOTIFF_SHORT:
               m_rows_per_strip = m_tags[tag_index].m_short_values[0];
               break;
            case GEOTIFF_LONG:
               m_rows_per_strip = m_tags[tag_index].m_long_values[0];
               break;
            default:
               // data is wrong type
               m_new_error_description = "RowsPerStrip tag is invalid data type.";
               return FAILURE;
         }
      }
      else
         m_rows_per_strip = 4294967295;   // default value is essentially infinity
      if( m_rows_per_strip == 0 )
      {
         m_new_error_description = "RowsPerStrip tag value is zero (invalid).";
         return FAILURE;
      }
      m_num_strips = m_image_length / m_rows_per_strip;  // complete strips
      if( m_image_length > m_num_strips * m_rows_per_strip )
         m_num_strips++;                                 // partial strip

      // find max strip size in bytes (no default)
      if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, tag_index ) == SUCCESS )
      {
         // check for number of strip byte counts = number of strips
//         if( m_tags[tag_index].m_count != m_num_strips )
//         {
//            m_new_error_description =
//               "StripByteCount tag count does not equal number of strips.";
//            return FAILURE;
//         }
         switch( m_tags[tag_index].m_type )
         {
            case GEOTIFF_SHORT:
               m_max_strip_byte_count = 0;
               for( i = 0; i < (int)m_num_strips; i++ )
               {
                  if( m_tags[tag_index].m_short_values[i] >
                     m_max_strip_byte_count )
                     m_max_strip_byte_count = m_tags[tag_index].m_short_values[i];
               }
               if( m_max_strip_byte_count == 0 )
               {
                  m_new_error_description = "StripByteCount is zero.";
                  return FAILURE;
               }
               break;
            case GEOTIFF_LONG:
               m_max_strip_byte_count = 0;
               for( i = 0; i < (int)m_num_strips; i++ )
               {
                  if( m_tags[tag_index].m_long_values[i] >
                      m_max_strip_byte_count )
                     m_max_strip_byte_count = m_tags[tag_index].m_long_values[i];
               }
               if( m_max_strip_byte_count == 0 )
               {
                  m_new_error_description = "StripByteCount is zero.";
                  return FAILURE;
               }
               break;
            default:
               // data is wrong type
               m_new_error_description = "RowsPerStrip tag is invalid data type.";
               return FAILURE;
         }
      }
      else
      {
         m_new_error_description = "StripByteCounts tag is missing.";
         return FAILURE;
      }
   }

   // get x resolution (default=1.0)
   if( find_tag( GEOTIFF_X_RESOLUTION_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_RATIONAL:
            m_x_resolution = m_tags[tag_index].m_double_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "XResolution tag is invalid data type.";
            return FAILURE;
      }
   }
   else
      m_x_resolution = 1.0;

   // get y resolution (default=1.0)
   if( find_tag( GEOTIFF_Y_RESOLUTION_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_RATIONAL:
            m_y_resolution = m_tags[tag_index].m_double_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "YResolution tag is invalid data type.";
            return FAILURE;
      }
   }
   else
      m_y_resolution = 1.0;

   // get resolution unit (default is inch)
   if( find_tag( GEOTIFF_RESOLUTION_UNIT_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_resolution_unit = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description =
               "ResolutionUnit tag is invalid data type.";
            return FAILURE;
      }
      switch( m_resolution_unit )
      {
         case GEOTIFF_RESOLUTION_UNIT_NONE:
         case GEOTIFF_RESOLUTION_UNIT_INCH:
         case GEOTIFF_RESOLUTION_UNIT_CENTIMETER:
            break;
         default:                       // any others not supported at present
            m_new_error_description =
               "ResolutionUnit tag value is not supported.";
            return FAILURE;
      }
   }
   else
      m_resolution_unit = GEOTIFF_RESOLUTION_UNIT_INCH;

   // get planar configuration (default is chunky format)
   if( find_tag( GEOTIFF_PLANAR_CONFIGURATION_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_planar_configuration = m_tags[tag_index].m_short_values[0];
			if (m_samples_per_pixel == 1)
				m_planar_configuration = 1;
            break;
         default:
            // data is wrong type
			 if (m_samples_per_pixel != 1)
			 {
				m_new_error_description =
				   "PlanarConfiguration tag is invalid data type.";
				return FAILURE;
			 }
      }
      switch( m_planar_configuration )
      {
         case GEOTIFF_CHUNKY_FORMAT:
            break;
         case GEOTIFF_PLANAR_FORMAT:
            break;
         default:                       // any others not supported at present
            m_new_error_description =
               "Image is in planar format which is not supported.";
            return FAILURE;
      }
   }
   else
      m_planar_configuration = GEOTIFF_CHUNKY_FORMAT;

   // get samples per pixel (default is 1)
   if( find_tag( GEOTIFF_SAMPLES_PER_PIXEL_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_samples_per_pixel = m_tags[tag_index].m_short_values[0];
            break;
         case GEOTIFF_LONG:
            m_samples_per_pixel = m_tags[tag_index].m_long_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description =
               "SamplesPerPixel tag is invalid data type.";
            return FAILURE;
      }
      switch( m_samples_per_pixel )
      {
         case 1:
         case 3:
            break;
         default:                       // any others not supported at present
            m_new_error_description =
               "SamplesPerPixel tag value is not supported.";
            return FAILURE;
      }
   }
   else
      m_samples_per_pixel = 1;

   // get bits per sample (default is 1)
   if( find_tag( GEOTIFF_BITS_PER_SAMPLE_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_bits_per_sample = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "BitsPerSample tag is invalid data type.";
            return FAILURE;
      }
      switch( m_bits_per_sample )
      {
         case 1:
         case 4:
         case 8:
         case 16:
            break;
         default:                     // any others not supported at present
            m_new_error_description =
               "BitsPerSample tag value is not supported.";
            return FAILURE;
      }
   }
   else
      m_bits_per_sample = 1;

   // get fill order (default is 1)
   if( find_tag( GEOTIFF_FILL_ORDER_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_fill_order = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "FillOrder tag is invalid data type.";
            return FAILURE;
      }
      switch( m_fill_order )
      {
         case 1:
            break;
         default:                       // any others not supported at present
            m_new_error_description = "FillOrder tag value is not supported.";
            return FAILURE;
      }
   }
   else
      m_fill_order = 1;

   // get orientation (default is 1)
   if( find_tag( GEOTIFF_ORIENTATION_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_orientation = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "Orientation tag is invalid data type.";
            return FAILURE;
      }
      switch( m_orientation )
      {
         case 1:
            break;
         default:                     // any others not supported at present
            m_new_error_description = "Orientation tag value is not supported.";
            return FAILURE;
      }
   }
   else
      m_orientation = 1;

   // color map
   if( m_photometric_interpretation == GEOTIFF_RGB_PALETTE )
   {
      if( find_tag( GEOTIFF_COLOR_MAP_TAG, tag_index ) == SUCCESS )
      {
         switch( m_tags[tag_index].m_type )
         {
            case GEOTIFF_SHORT:
               count = m_tags[tag_index].m_count;
               m_num_color_map_values = 2 << (m_bits_per_sample-1);
               if( count != 3 * m_num_color_map_values )
               {
                  m_new_error_description =
                     "ColorMap tag contains incorrect data count.";
                  return FAILURE;   // wrong size
               }
               // allocate memory for color map arrays
               m_red_color_map = new unsigned short[m_num_color_map_values];
               if (m_red_color_map == nullptr)
                  return FAILURE;
               m_green_color_map = new unsigned short[m_num_color_map_values];
               if (m_green_color_map == nullptr)
                  return FAILURE;
               m_blue_color_map = new unsigned short[m_num_color_map_values];
               if (m_blue_color_map == nullptr)
                  return FAILURE;
               // copy data from color map tag into color map arrays:
               // first red, then green, then blue
               index = 0;
               for( i = 0; i < m_num_color_map_values; i++, index++ )
                  m_red_color_map[i] = m_tags[tag_index].m_short_values[index];
               for( i = 0; i < m_num_color_map_values; i++, index++ )
                  m_green_color_map[i] =
                     m_tags[tag_index].m_short_values[index];
               for( i = 0; i < m_num_color_map_values; i++, index++ )
                  m_blue_color_map[i] = m_tags[tag_index].m_short_values[index];
               m_color_map_present = TRUE;
               break;
            default:
               // data is wrong type
               m_new_error_description = "ColorMap tag is invalid data type.";
               return FAILURE;
         }
      }
      else
      {
         m_new_error_description = "ColorMap tag is missing.";
         return FAILURE;   // color map must be present for GEOTIFF_RGB_PALETTE
      }
   }

   // min sample value (default value is 0)
   if( find_tag( GEOTIFF_MIN_SAMPLE_VALUE_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_min_sample_value = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "Min sample value tag is invalid data type.";
            return FAILURE;
      }
   }
   else
      m_min_sample_value = 0;   // default value

   // max sample value (default is all bits set)
   if( find_tag( GEOTIFF_MAX_SAMPLE_VALUE_TAG, tag_index ) == SUCCESS )
   {
      switch( m_tags[tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            m_max_sample_value = m_tags[tag_index].m_short_values[0];
            break;
         default:
            // data is wrong type
            m_new_error_description = "Max sample value tag is invalid data type.";
            return FAILURE;
      }
   }
   else
      m_max_sample_value = (2 << m_bits_per_sample) - 1;    // default value

   // check for valid max/min sample values
   // use defaults if invalid
   if( m_max_sample_value <= m_min_sample_value )
   {
      m_min_sample_value = 0;
      m_max_sample_value = (2 << m_bits_per_sample) - 1;
   }

//   remove before flight!
//   m_min_sample_value = 0;
//   m_max_sample_value = 2047;

   // determine image type
   switch( m_photometric_interpretation )
   {
      case GEOTIFF_WHITE_IS_ZERO:
         switch( m_bits_per_sample )
         {
            case 1:
               m_image_type_supported = FALSE;
               m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
               m_image_type_description = "BiLevel Image with White = Zero";
               break;
            case 4:
               m_image_type_supported = FALSE;
               m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
               m_image_type_description =
                  "16-Shade Grayscale Image with White = Zero";
               break;
            case 8:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_256_GRAYSCALE;
               m_image_type_description =
                  "256-Shade Grayscale Image with White = Zero";
               break;
            case 16:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE;
               m_image_type_description =
                  "16-Bit Grayscale Image with White = Zero";
               break;
            default:
               m_image_type_supported = FALSE;
               m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
               m_image_type_description = "Image type is unknown.";
               m_new_error_description = "Image type is unknown.";
               return FAILURE;            // others not supported
         }
         break;
      case GEOTIFF_BLACK_IS_ZERO:
         switch( m_bits_per_sample )
         {
            case 1:
               m_image_type_supported = FALSE;
               m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
               m_image_type_description = "BiLevel Image with Black = Zero";
               break;
            case 4:
               m_image_type_supported = FALSE;
               m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
               m_image_type_description =
                  "16-Shade Grayscale Image with Black = Zero";
               break;
            case 8:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_256_GRAYSCALE;
               m_image_type_description =
                  "256-Shade Grayscale Image with Black = Zero";
               break;
            case 16:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE;
               m_image_type_description =
                  "16-Bit Grayscale Image with Black = Zero";
               break;
            default:
               m_image_type_supported = FALSE;
               m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
               m_image_type_description = "Image type is unknown.";
               m_new_error_description = "Image type is unknown.";
               return FAILURE;            // others not supported
         }
         break;
      case GEOTIFF_RGB:
      case GEOTIFF_YCBCR:
         switch( m_bits_per_sample )
         {
            case 8:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_24BIT_COLOR;
               m_image_type_description = "24-Bit RGB Image";
               break;
            default:
               m_image_type_supported = FALSE;
               m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
               m_image_type_description = "Image type is unknown.";
               m_new_error_description = "Image type unknown.";
               return FAILURE;                      // only 8-bit supported
         }
         break;
      case GEOTIFF_RGB_PALETTE:
         switch( m_bits_per_sample )
         {
            case 4:
               m_image_type_supported = FALSE;
               m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
               m_image_type_description = "16-Color Palette Image";
               break;
            case 8:
               m_image_type_supported = TRUE;
               m_image_type = GEOTIFF_IMAGE_TYPE_256_COLOR;
               m_image_type_description = "256-Color Palette Image";
               break;
            default:
               m_image_type_supported = FALSE;
               m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
               m_image_type_description = "Image type is unknown.";
               m_new_error_description = "Image type unknown.";
               return FAILURE;                      // only 8-bit supported
         }
         break;
      default:
         m_image_type_supported = FALSE;
         m_image_type = GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED;
         m_image_type_description = "Image type is unknown.";
         m_new_error_description = "Image type is unknown.";
         return FAILURE;
   }

   // check for case of large strips
   // in this case we try to change to lots of small strips
   // for more efficient reading
   if( m_image_type_supported &&
       m_compression_scheme == GEOTIFF_NO_COMPRESSION &&
       m_rows_per_strip > 5 &&
       m_max_strip_byte_count > 1000000 )
   {
      int strip_byte_counts_tag_index, strip_offsets_tag_index;
      int bytes_per_pixel;
      unsigned int istrip, inewstrip, irow, row_offset;
      unsigned int strip_byte_count, strip_offset;
      unsigned int new_rows_per_strip;
      unsigned int new_num_strips;
      unsigned int new_max_strip_byte_count;
      CTiffTag new_strip_byte_counts_tag;
      CTiffTag new_strip_offsets_tag;

      new_rows_per_strip = 1;
      new_num_strips = m_image_length;

      switch( m_image_type )
      {
         case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
         case GEOTIFF_IMAGE_TYPE_256_COLOR:
            bytes_per_pixel = 1;
            break;
         case GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE:
            bytes_per_pixel = 2;
            break;
         case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
            bytes_per_pixel = 3;
            break;
         default:
            goto DONE;
      }
      new_max_strip_byte_count = m_image_width * bytes_per_pixel;

      // find strip byte count and offsets tags
      find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index );
      find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index );

      // create new strip byte counts tag
      new_strip_byte_counts_tag.m_defined = TRUE;
      new_strip_byte_counts_tag.m_tag_id = GEOTIFF_STRIP_BYTE_COUNTS_TAG;
      new_strip_byte_counts_tag.m_type = GEOTIFF_LONG;
      new_strip_byte_counts_tag.m_count = new_num_strips;
      new_strip_byte_counts_tag.m_value_offset =
         m_tags[strip_byte_counts_tag_index].m_value_offset;
      new_strip_byte_counts_tag.m_tag_name =
         m_tags[strip_byte_counts_tag_index].m_tag_name;
      new_strip_byte_counts_tag.m_type_name = "LONG";

      new_strip_byte_counts_tag.m_long_values =
         new unsigned int[new_num_strips];
      if (new_strip_byte_counts_tag.m_long_values == nullptr)
         return FAILURE;

      // create new strip offsets tag
      new_strip_offsets_tag.m_defined = TRUE;
      new_strip_offsets_tag.m_tag_id = GEOTIFF_STRIP_OFFSETS_TAG;
      new_strip_offsets_tag.m_type = GEOTIFF_LONG;
      new_strip_offsets_tag.m_count = new_num_strips;
      new_strip_offsets_tag.m_value_offset =
         m_tags[strip_offsets_tag_index].m_value_offset;
      new_strip_offsets_tag.m_tag_name =
         m_tags[strip_offsets_tag_index].m_tag_name;
      new_strip_offsets_tag.m_type_name = "LONG";
      new_strip_offsets_tag.m_long_values = new unsigned int[new_num_strips];
      if (new_strip_offsets_tag.m_long_values == nullptr)
         return FAILURE;

      // fill in new strip byte counts and offsets
      inewstrip = 0;
      for( istrip = 0; istrip < m_num_strips; istrip++ )
      {
         switch( m_tags[strip_byte_counts_tag_index].m_type )
         {
            case GEOTIFF_SHORT:
               strip_byte_count =
                  m_tags[strip_byte_counts_tag_index].m_short_values[istrip];
               break;
            case GEOTIFF_LONG:
               strip_byte_count =
                  m_tags[strip_byte_counts_tag_index].m_long_values[istrip];
               break;
         }
         switch( m_tags[strip_offsets_tag_index].m_type )
         {
            case GEOTIFF_SHORT:
               strip_offset =
                  m_tags[strip_offsets_tag_index].m_short_values[istrip];
               break;
            case GEOTIFF_LONG:
               strip_offset =
                  m_tags[strip_offsets_tag_index].m_long_values[istrip];
               break;
         }
         for( irow = 0; irow < m_rows_per_strip; irow++ )
         {
            row_offset = strip_offset + irow * new_max_strip_byte_count;
            new_strip_byte_counts_tag.m_long_values[inewstrip] =
               new_max_strip_byte_count;
            new_strip_offsets_tag.m_long_values[inewstrip] =
               row_offset;
            inewstrip++;
            if( inewstrip == new_num_strips )
               break;
         }
      }

      // assign the value name strings
      new_strip_byte_counts_tag.m_value_name =
         get_tag_value_name( new_strip_byte_counts_tag );
      new_strip_offsets_tag.m_value_name =
         get_tag_value_name( new_strip_offsets_tag );

      // now replace the old byte count and offset tags with the new ones
      m_tags[strip_byte_counts_tag_index].clear( );
      m_tags[strip_byte_counts_tag_index] = new_strip_byte_counts_tag;
      new_strip_byte_counts_tag.m_long_values = NULL;
      new_strip_byte_counts_tag.clear( );

      m_tags[strip_offsets_tag_index].clear( );
      m_tags[strip_offsets_tag_index] = new_strip_offsets_tag;
      new_strip_offsets_tag.m_long_values = NULL;
      new_strip_offsets_tag.clear( );

      // set the new values
      m_rows_per_strip = new_rows_per_strip;
      m_num_strips = new_num_strips;
      m_max_strip_byte_count = new_max_strip_byte_count;
   }

DONE:
   return SUCCESS;
}
// end of inspect_tags

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::read_geokey_directory( )
{
   // this function finds the geokey directory, if present, and
   // reads the geokey directory and all geokeys
   // returns SUCCESS or FAILURE

   int i, tag_index, value_index;
   int j, itag, offset, count;
   const int STRING_LEN = 50;
   char string[STRING_LEN];

   // find the index of the geokey directory if present
   if( find_tag( GEOTIFF_GEOKEY_DIRECTORY_TAG, tag_index ) == FAILURE )
      return SUCCESS;   // if geokey directory is not present,
                        // return SUCCESS anyway

   // verify that tag data is of type GEOTIFF_SHORT
   if( m_tags[tag_index].m_type != GEOTIFF_SHORT )
   {
      m_new_error_description = "GeoKeyDirectoryTag tag is invalid data type.";
      return FAILURE;
   }

   // verify that tag contains at least enough data for the geokey directory
   // header
   if( m_tags[tag_index].m_count < 4 )
   {
      m_new_error_description =
         "GeoKeyDirectoryTag tag does not contain geokey directory header.";
      return FAILURE;
   }

   // get geokey directory header info from tag array
   m_geokey_directory_version = m_tags[tag_index].m_short_values[0];
   m_geokey_directory_revision = m_tags[tag_index].m_short_values[1];
   m_geokey_directory_minor_revision = m_tags[tag_index].m_short_values[2];
   m_num_geokeys = m_tags[tag_index].m_short_values[3];

   // if number of geokeys is zero, return SUCCESS
   if( m_num_geokeys == 0 )
	   return SUCCESS;

   // see if the tag has enough data for the directory header and all the
   // geokey entries
   if( (int)m_tags[tag_index].m_count < 4 * ( m_num_geokeys + 1 ) )
   {
      m_new_error_description =
         "GeoKeyDirectoryTag tag has incorrect data count.";
      return FAILURE;
   }

   // allocate memory for geokeys
   m_geokeys = new CGeoKey[m_num_geokeys];
   if (m_geokeys == nullptr)
      return FAILURE;

   // index into the tag data array
   value_index = 4;

   // read each geokey entry from the tag array data
   for( i = 0; i < m_num_geokeys; i++ )
   {
      m_geokeys[i].clear( );
      m_geokeys[i].m_geokey_id = m_tags[tag_index].m_short_values[value_index];
      value_index++;
      m_geokeys[i].m_tiff_tag_location =
         m_tags[tag_index].m_short_values[value_index];
      value_index++;
      m_geokeys[i].m_count = m_tags[tag_index].m_short_values[value_index];
      value_index++;
      m_geokeys[i].m_value_offset =
         m_tags[tag_index].m_short_values[value_index];
      value_index++;

      // check for data contained in value offset (m_tiff_tag_location = 0)
      if( m_geokeys[i].m_tiff_tag_location == 0 )
      {
         // data is a single unsigned short contained in value_offset
         m_geokeys[i].m_count = 1;
         m_geokeys[i].m_type = GEOTIFF_SHORT;
         m_geokeys[i].m_short_values = new unsigned short;
         if (m_geokeys[i].m_short_values == nullptr)
            return FAILURE;
         m_geokeys[i].m_short_values[0] = m_geokeys[i].m_value_offset;
      }
      else
      {
         // data is contained in the tag indicated by tiff tag location

         // check that indicated tag exists by finding its index itag
         itag = -1;
         for( j = 0; j < m_num_tags; j++ )
         {
            if( m_tags[j].m_tag_id == m_geokeys[i].m_tiff_tag_location )
            {
               itag = j;
               break;
            }
         }
         if( itag == -1 )
         {
            sprintf_s( string, STRING_LEN, "Geokey %i references non-existant tag.", i );
            m_new_error_description = string;
            return FAILURE;
         }

         // offset, and count are used for shorthand
         offset = m_geokeys[i].m_value_offset;
         count = m_geokeys[i].m_count;

         // check for count compatibility
         if( (unsigned int)(offset + count) > m_tags[itag].m_count )
         {
            sprintf_s( string, STRING_LEN, "Geokey %i has invalid data count", i );
            m_new_error_description = string;
            return FAILURE;
         }

         // data type is the type of the tag data
         m_geokeys[i].m_type = m_tags[itag].m_type;

         // allocate memory for data and copy from tag array
         switch( m_geokeys[i].m_type )
         {
            case GEOTIFF_ASCII:             // null-terminated string
               m_geokeys[i].m_ascii_values = new char[count];
               if (m_geokeys[i].m_ascii_values == nullptr)
                  return FAILURE;
               // copy data from tag at value offset
               for( j = 0; j < count; j++ )
                  m_geokeys[i].m_ascii_values[j] =
                     m_tags[itag].m_ascii_values[offset+j];
               // add terminating null
               m_geokeys[i].m_ascii_values[count-1] = NULL;
               break;
            case GEOTIFF_SHORT:             // unsigned short (16-bit) integer
               m_geokeys[i].m_short_values = new unsigned short[count];
               if (m_geokeys[i].m_short_values == nullptr)
                  return FAILURE;
               // copy data from tag at value offset
               for( j = 0; j < count; j++ )
                  m_geokeys[i].m_short_values[j] =
                  m_tags[itag].m_short_values[offset+j];
               break;
            case GEOTIFF_DOUBLE:            // 8-byte double value
               m_geokeys[i].m_double_values = new double[count];
               if (m_geokeys[i].m_double_values == nullptr)
                  return FAILURE;
               // copy data from tag at value offset
               for( j = 0; j < count; j++ )
                  m_geokeys[i].m_double_values[j] =
                     m_tags[itag].m_double_values[offset+j];
               break;
            default:
               // data type is unknown - don't read values
               break;
         }

      }

      // assign names to geokey, type, and value
      m_geokeys[i].m_geokey_name = get_geokey_name( m_geokeys[i].m_geokey_id );
      m_geokeys[i].m_type_name = get_type_name( m_geokeys[i].m_type );
      m_geokeys[i].m_value_name = get_geokey_value_name( m_geokeys[i] );

      // geokey is now completely defined
      m_geokeys[i].m_defined = TRUE;

   }

   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::find_geokey( unsigned short geokey_id, int &index )
{
   // finds the index of the specified geokey_id in the geokey array
   // returns SUCCESS or FAILURE

   for( index = 0; index < m_num_geokeys; index++ )
      if( m_geokeys[index].m_geokey_id == geokey_id )
		  return SUCCESS;

   index = -1;
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::decompress_packbits( int compressed_size,
                                   unsigned char *compressed,
                                   int decompressed_size,
                                   unsigned char *decompressed )
{
   // this function decompresses an array of bytes using the packbits algorithm
   // compressed_size is the number of bytes in the compressed array - not all
   // of the compressed bytes will necessarily be used.
   // decompressed_size is the number of bytes expected from the
   // decompression -  normally determind by the number of rows in a strip of
   // data
   // the decompressed data is stored in the decompressed array
   // returns SUCCESS or FAILURE

   int count;
   signed char sbyte;

	signed char *comp = (signed char*)compressed;
	signed char *comp_end = comp + compressed_size;
	unsigned char *decomp = decompressed;
	unsigned char *decomp_end = decomp + decompressed_size;

   int overcount = 0;
   while( comp < comp_end )
   {
      // get a signed byte from the compressed array and increment index
      sbyte = *comp++;

      if( sbyte == -128 )
      {
			comp++;
         if( comp >= comp_end)
			 return FAILURE;
         continue;
      }

      if( sbyte >= 0 )
      {
         // if sbyte is positive or zero, we copy the next sbyte+1 literally
         count = sbyte + 1;

         // check for overflow
         if( comp + count > comp_end)
			 return FAILURE;
         if( decomp + count > decomp_end)
			   count = decomp_end - decomp;

         // copy the data
			memcpy(decomp, comp, count);

         // increment indices
			comp += count;
			decomp += count;

         // check for done
         if( decomp == decomp_end)
			 return SUCCESS;

         continue;
      }

		// otherwise, if sbyte is negative, we duplicate the next 1-sbyte times
		count = 1 - sbyte;

		// check for overflow
		if( comp >= comp_end)
			return FAILURE;
		if( decomp + count > decomp_end)
			count = decomp_end - decomp;

		// duplicate the data
		memset(decomp, *comp++, count);

		// increment indices
		decomp += count;

		// check for done
		if( decomp == decomp_end)
			return SUCCESS;
   }

   // if we run out of compressed bytes before finishing, return FAILURE
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::decompress_lzw( int compressed_size,
                                   unsigned char *compressed,
                                   int decompressed_size,
                                   unsigned char *decompressed )
{
   // this function decompresses an array of bytes using the TIFF 6.0 variant
   // of LZW (compression tag 5).  Same contract as decompress_packbits:
   // compressed_size is the number of bytes available - not all of them will
   // necessarily be used; decompressed_size is the number of bytes expected,
   // and decoding stops as soon as that many have been produced.
   // returns SUCCESS or FAILURE
   //
   // Codes are packed most-significant-bit first (fill order 2 is rejected at
   // load time, so there is no bit-reversed case to handle here).  Widths run
   // 9..12 bits and step up ONE CODE EARLY - the off-by-one in the TIFF 6.0
   // pseudocode that every encoder in the wild reproduces.  Codes 256 and 257
   // are Clear and EndOfInformation; 258 up are string table entries.

   const int clear_code = 256;
   const int eoi_code = 257;
   const int first_entry = 258;
   const int max_entries = 4096;

   // the string table: each entry is a prefix code plus one suffix byte, so a
   // string is walked backwards down the prefix chain and emitted reversed
   short prefix[max_entries];
   unsigned char suffix[max_entries];
   unsigned char reversed[max_entries];

   int i, code, old_code, code_width, next_entry;
   int walk, nbytes;
   unsigned int bit_pos, bit_end, byte_index, accum;
   unsigned char first_char;
   unsigned char *decomp = decompressed;
   unsigned char *decomp_end = decomp + decompressed_size;

   if( compressed_size <= 0 || decompressed_size <= 0 )
      return FAILURE;

   for( i = 0; i < 256; i++ )
   {
      prefix[i] = -1;
      suffix[i] = (unsigned char)i;
   }

   bit_pos = 0;
   bit_end = (unsigned int)compressed_size * 8;

   code_width = 9;
   next_entry = first_entry;
   old_code = -1;
   first_char = 0;

   while( bit_pos + (unsigned int)code_width <= bit_end )
   {
      // read code_width bits, most significant bit first
      byte_index = bit_pos >> 3;
      accum = (unsigned int)compressed[byte_index] << 16;
      accum |= (unsigned int)compressed[byte_index+1] << 8;
      if( byte_index + 2 < (unsigned int)compressed_size )
         accum |= (unsigned int)compressed[byte_index+2];
      code = (int)( ( accum >> ( 24 - ( bit_pos & 7 ) - code_width ) ) &
                    ( ( 1u << code_width ) - 1 ) );
      bit_pos += (unsigned int)code_width;

      if( code == eoi_code )
         break;

      if( code == clear_code )
      {
         code_width = 9;
         next_entry = first_entry;
         old_code = -1;
         continue;
      }

      if( old_code == -1 )
      {
         // first code after a Clear must be a literal
         if( code >= 256 )
            return FAILURE;
         if( decomp >= decomp_end )
            return SUCCESS;
         *decomp++ = (unsigned char)code;
         if( decomp == decomp_end )
            return SUCCESS;
         old_code = code;
         first_char = (unsigned char)code;
         continue;
      }

      if( code < next_entry )
      {
         // the code is in the table: emit its string, then add old_code plus
         // that string's first character as the next entry
         walk = code;
      }
      else if( code == next_entry && next_entry < max_entries )
      {
         // the KwKwK case: the encoder used an entry it had only just added,
         // so the string is old_code's string plus its own first character
         walk = -1;
      }
      else
      {
         return FAILURE;
      }

      if( walk >= 0 )
      {
         nbytes = 0;
         while( walk >= 0 && nbytes < max_entries )
         {
            reversed[nbytes++] = suffix[walk];
            walk = prefix[walk];
         }
         if( walk >= 0 )
            return FAILURE;                   // corrupt prefix chain
         first_char = reversed[nbytes-1];
      }
      else
      {
         nbytes = 0;
         walk = old_code;
         while( walk >= 0 && nbytes < max_entries )
         {
            reversed[nbytes++] = suffix[walk];
            walk = prefix[walk];
         }
         if( walk >= 0 )
            return FAILURE;
         first_char = reversed[nbytes-1];
         // prepend (in reversed order, append) the leading character
         if( nbytes >= max_entries )
            return FAILURE;
         for( i = nbytes; i > 0; i-- )
            reversed[i] = reversed[i-1];
         reversed[0] = first_char;
         nbytes++;
      }

      for( i = nbytes - 1; i >= 0; i-- )
      {
         if( decomp >= decomp_end )
            return SUCCESS;                   // caller wanted only this much
         *decomp++ = reversed[i];
      }

      // add old_code + first character of what we just emitted
      if( next_entry < max_entries )
      {
         prefix[next_entry] = (short)old_code;
         suffix[next_entry] = first_char;
         next_entry++;
      }

      old_code = code;

      // "early change": the width steps up one code before the table is full
      if( next_entry + 1 >= ( 1 << code_width ) && code_width < 12 )
         code_width++;

      if( decomp == decomp_end )
         return SUCCESS;
   }

   // ran out of codes: SUCCESS only if we produced everything asked for
   return ( decomp == decomp_end ) ? SUCCESS : FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::undo_horizontal_predictor( unsigned char *data,
                                                  int size, int row_bytes )
{
   // undoes TIFF predictor 2 (horizontal differencing) in place.  Each sample
   // was stored as its difference from the sample one PIXEL to its left, so
   // the stride is samples-per-pixel and each row restarts.
   // returns SUCCESS or FAILURE

   int row_start, i, spp;

   if( row_bytes <= 0 )
      return FAILURE;

   spp = (int)m_samples_per_pixel;
   if( spp <= 0 )
      return FAILURE;

   if( m_bits_per_sample == 8 )
   {
      for( row_start = 0; row_start < size; row_start += row_bytes )
      {
         int row_len = __min( row_bytes, size - row_start );
         unsigned char *row = data + row_start;
         for( i = spp; i < row_len; i++ )
            row[i] = (unsigned char)( row[i] + row[i-spp] );
      }
      return SUCCESS;
   }

   if( m_bits_per_sample == 16 )
   {
      // differencing is over 16-bit samples; the bytes are in file order, so
      // reassemble each sample with the file's byte order and write it back
      int stride = spp * 2;
      for( row_start = 0; row_start < size; row_start += row_bytes )
      {
         int row_len = __min( row_bytes, size - row_start );
         unsigned char *row = data + row_start;
         for( i = stride; i + 1 < row_len; i += 2 )
         {
            unsigned int prev, cur;
            if( m_byte_order == GEOTIFF_BIG_ENDIAN )
            {
               prev = ( (unsigned int)row[i-stride] << 8 ) | row[i-stride+1];
               cur  = ( (unsigned int)row[i] << 8 ) | row[i+1];
               cur = ( cur + prev ) & 0xFFFF;
               row[i]   = (unsigned char)( cur >> 8 );
               row[i+1] = (unsigned char)( cur & 0xFF );
            }
            else
            {
               prev = ( (unsigned int)row[i-stride+1] << 8 ) | row[i-stride];
               cur  = ( (unsigned int)row[i+1] << 8 ) | row[i];
               cur = ( cur + prev ) & 0xFFFF;
               row[i+1] = (unsigned char)( cur >> 8 );
               row[i]   = (unsigned char)( cur & 0xFF );
            }
         }
      }
      return SUCCESS;
   }

   return FAILURE;                             // predictor 2 needs 8 or 16 bpp
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::decompress_strip( int compressed_size,
                                   unsigned char *compressed,
                                   int decompressed_size,
                                   unsigned char *decompressed )
{
   // one entry point for every reader below: pick the codec, then undo the
   // predictor.  With PackBits and predictor 1 - everything this reader
   // handled before LZW was added - it is decompress_packbits and nothing
   // else, byte for byte.
   // returns SUCCESS or FAILURE

   int row_bytes;

   switch( m_compression_scheme )
   {
      case GEOTIFF_PACKBITS:
         if( decompress_packbits( compressed_size, compressed,
                                  decompressed_size, decompressed ) != SUCCESS )
            return FAILURE;
         break;
      case GEOTIFF_LZW:
         if( decompress_lzw( compressed_size, compressed,
                             decompressed_size, decompressed ) != SUCCESS )
            return FAILURE;
         break;
      default:
         return FAILURE;
   }

   if( m_predictor == 2 )
   {
      // a row of the strip, or of the tile when the image is tiled
      row_bytes = (int)( m_image_is_tiled ? m_tile_width : m_image_width ) *
                  (int)m_samples_per_pixel * ( (int)m_bits_per_sample / 8 );
      if( undo_horizontal_predictor( decompressed, decompressed_size,
                                     row_bytes ) != SUCCESS )
         return FAILURE;
   }

   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_8bit_grayscale_as_rgb_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array )
{
   // this function reads a grayscale (8-bit monochrome) subimage into the
   // argument red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i, istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   int decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int strip_offset, strip_byte_count;
   unsigned char red, *strip_data, *decompressed_strip_data;

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
	   goto FAIL;

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
	   goto FAIL;

   // allocate data for strips
   strip_data = NULL;
   strip_data = new unsigned char[m_max_strip_byte_count];
   if (strip_data == nullptr)
      goto FAIL;

   // if compressed, allocate memory for a decompressed strip
   decompressed_strip_data = NULL;
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data =
         new unsigned char[m_image_width * m_rows_per_strip *
                           m_samples_per_pixel];
      if (decompressed_strip_data == nullptr)
         goto FAIL;
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   // loop through each strip
   for( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      // get the strip offset
      switch( m_tags[strip_offsets_tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            strip_offset =
               m_tags[strip_offsets_tag_index].m_short_values[istrip];
            break;
         case GEOTIFF_LONG:
            strip_offset =
               m_tags[strip_offsets_tag_index].m_long_values[istrip];
            break;
         default:
            goto FAIL;
      }
      // get the strip byte count
      switch( m_tags[strip_byte_counts_tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            strip_byte_count =
               m_tags[strip_byte_counts_tag_index].m_short_values[istrip];
            break;
         case GEOTIFF_LONG:
            strip_byte_count =
               m_tags[strip_byte_counts_tag_index].m_long_values[istrip];
            break;
         default:
            goto FAIL;
      }

      // set the file pointer to the row offset
      if( fseek( m_file_ptr, strip_offset, SEEK_SET ) != 0 )
		  goto FAIL;

      // read the strip
      if( fread( strip_data, strip_byte_count, 1, m_file_ptr ) != 1 )
		  goto FAIL;

      switch( m_compression_scheme )
      {
         case GEOTIFF_NO_COMPRESSION:
            // copy strip data to pixel array
            for( i = 0; i < (int)strip_byte_count; i++ )
            {
               if( hpix >= min_hpix && hpix <= max_hpix &&
                   vpix >= min_vpix && vpix <= max_vpix )
               {
                  red = strip_data[i];
                  // check for reversed grayscale
                  if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     red = 255 - red;
                  red_array[subimage_pixel_index] = red;
                  green_array[subimage_pixel_index] = red;
                  blue_array[subimage_pixel_index] = red;
                  subimage_pixel_index++;
               }
               hpix++;
               if( hpix == (int)m_image_width )
               {
                  hpix = 0;
                  vpix++;
                  if( vpix > max_vpix )
					  goto DONE;
               }
               ipixel++;
            }
            break;
         case GEOTIFF_CCITT_1D:
            goto FAIL;                          // not yet supported
         case GEOTIFF_LZW:                      // LZW compression scheme
         case GEOTIFF_PACKBITS:                 // PackBits compression scheme
            // copy decompressed strip data to pixel array
            // decompress data
            num_remaining_bytes =
               (m_num_pixels - ipixel) * m_samples_per_pixel;
            decompressed_size =
               __min( (int)(m_image_width * m_rows_per_strip *
                  m_samples_per_pixel), num_remaining_bytes );
            if( decompress_strip( (int)strip_byte_count, strip_data,
                decompressed_size, decompressed_strip_data ) != SUCCESS )
                goto FAIL;
            for( i = 0; i < decompressed_size; i++ )
            {
               if( hpix >= min_hpix && hpix <= max_hpix &&
                   vpix >= min_vpix && vpix <= max_vpix )
               {
                  red = decompressed_strip_data[i];
                  // check for reversed grayscale
                  if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     red = 255 - red;
                  red_array[subimage_pixel_index] = red;
                  green_array[subimage_pixel_index] = red;
                  blue_array[subimage_pixel_index] = red;
                  subimage_pixel_index++;
               }
               hpix++;
               if( hpix == (int)m_image_width )
               {
                  hpix = 0;
                  vpix++;
                  if( vpix > max_vpix )
					  goto DONE;
               }
               ipixel++;
            }
            break;
         default:
            goto FAIL;
      }

   }

DONE:
   // deallocate strip data array
   if( strip_data != NULL )
   {
      delete [] strip_data;
      strip_data = NULL;
   }

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      delete [] decompressed_strip_data;
      decompressed_strip_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      delete [] strip_data;
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      delete [] decompressed_strip_data;
      decompressed_strip_data = NULL;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_16bit_grayscale_as_rgb_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array )
{
   // this function reads a 16-bit grayscale subimage into the
   // argument red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i, istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   int decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int strip_offset, strip_byte_count;
   unsigned char red, *strip_data, *decompressed_strip_data;
   unsigned char byte1, byte2;
   unsigned short *strip_16bit_data;
   double fraction;

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
	   goto FAIL;

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
	   goto FAIL;

   // allocate data for strips
   strip_data = NULL;
   strip_data = new unsigned char[m_max_strip_byte_count];
   if (strip_data == nullptr)
      goto FAIL;

   // if compressed, allocate memory for a decompressed strip
   decompressed_strip_data = NULL;
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data =
         new unsigned char[2 * m_image_width * m_rows_per_strip *
                           m_samples_per_pixel];
      if (decompressed_strip_data == nullptr)
         goto FAIL;
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   // loop through each strip
   for( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      // get the strip offset
      switch( m_tags[strip_offsets_tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            strip_offset =
               m_tags[strip_offsets_tag_index].m_short_values[istrip];
            break;
         case GEOTIFF_LONG:
            strip_offset =
               m_tags[strip_offsets_tag_index].m_long_values[istrip];
            break;
         default:
            goto FAIL;
      }
      // get the strip byte count
      switch( m_tags[strip_byte_counts_tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            strip_byte_count =
               m_tags[strip_byte_counts_tag_index].m_short_values[istrip];
            break;
         case GEOTIFF_LONG:
            strip_byte_count =
               m_tags[strip_byte_counts_tag_index].m_long_values[istrip];
            break;
         default:
            goto FAIL;
      }

      // set the file pointer to the row offset
      if( fseek( m_file_ptr, strip_offset, SEEK_SET ) != 0 )
		  goto FAIL;

      // read the strip
      if( fread( strip_data, strip_byte_count, 1, m_file_ptr ) != 1 )
		  goto FAIL;

      switch( m_compression_scheme )
      {
         case GEOTIFF_NO_COMPRESSION:
            // swap byte order if necessary
            if( m_byte_order == GEOTIFF_BIG_ENDIAN )
            {
               for( i = 0; i < (int)strip_byte_count; i += 2 )
               {
                  byte1 = strip_data[i];
                  byte2 = strip_data[i+1];
                  strip_data[i] = byte2;
                  strip_data[i+1] = byte1;
               }
            }
            // copy strip data to pixel array
            strip_16bit_data = (unsigned short*)strip_data;
            for( i = 0; i < (int)strip_byte_count/2; i++ )
            {
               if( hpix >= min_hpix && hpix <= max_hpix &&
                   vpix >= min_vpix && vpix <= max_vpix )
               {
                  fraction = (double)(strip_16bit_data[i]-m_min_sample_value)/
                     (m_max_sample_value-m_min_sample_value);
                  if( fraction < 0.0 )
                     fraction = 0.0;
                  if( fraction > 1.0 )
                     fraction = 1.0;
                  red = (unsigned char)(255*fraction+0.5);
                  // check for reversed grayscale
                  if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     red = 255 - red;
                  red_array[subimage_pixel_index] = red;
                  green_array[subimage_pixel_index] = red;
                  blue_array[subimage_pixel_index] = red;
                  subimage_pixel_index++;
               }
               hpix++;
               if( hpix == (int)m_image_width )
               {
                  hpix = 0;
                  vpix++;
                  if( vpix > max_vpix )
					  goto DONE;
               }
               ipixel++;
            }
            break;
         case GEOTIFF_CCITT_1D:
            goto FAIL;                          // not yet supported
         case GEOTIFF_LZW:                      // LZW compression scheme
         case GEOTIFF_PACKBITS:                 // PackBits compression scheme
            // copy decompressed strip data to pixel array
            // decompress data
            num_remaining_bytes =
               2 * (m_num_pixels - ipixel) * m_samples_per_pixel;
            decompressed_size =
               __min( (int)(2 * m_image_width * m_rows_per_strip *
                  m_samples_per_pixel), num_remaining_bytes );
            if( decompress_strip( (int)strip_byte_count, strip_data,
                decompressed_size, decompressed_strip_data ) != SUCCESS )
                goto FAIL;
            // swap byte order if necessary
            if( m_byte_order == GEOTIFF_BIG_ENDIAN )
            {
               for( i = 0; i < (int)decompressed_size; i += 2 )
               {
                  byte1 = decompressed_strip_data[i];
                  byte2 = decompressed_strip_data[i+1];
                  decompressed_strip_data[i] = byte2;
                  decompressed_strip_data[i+1] = byte1;
               }
            }
            strip_16bit_data = (unsigned short*)decompressed_strip_data;
            for( i = 0; i < decompressed_size/2; i++ )
            {
               if( hpix >= min_hpix && hpix <= max_hpix &&
                   vpix >= min_vpix && vpix <= max_vpix )
               {
                  fraction = (double)(strip_16bit_data[i]-m_min_sample_value)/
                     (m_max_sample_value-m_min_sample_value);
                  if( fraction < 0.0 )
                     fraction = 0.0;
                  if( fraction > 1.0 )
                     fraction = 1.0;
                  red = (unsigned char)(255*fraction+0.5);
                  // check for reversed grayscale
                  if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
                     red = 255 - red;
                  red_array[subimage_pixel_index] = red;
                  green_array[subimage_pixel_index] = red;
                  blue_array[subimage_pixel_index] = red;
                  subimage_pixel_index++;
               }
               hpix++;
               if( hpix == (int)m_image_width )
               {
                  hpix = 0;
                  vpix++;
                  if( vpix > max_vpix )
					  goto DONE;
               }
               ipixel++;
            }
            break;
         default:
            goto FAIL;
      }

   }

DONE:
   // deallocate strip data array
   if( strip_data != NULL )
   {
      delete [] strip_data;
      strip_data = NULL;
   }

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      delete [] decompressed_strip_data;
      decompressed_strip_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      delete [] strip_data;
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      delete [] decompressed_strip_data;
      decompressed_strip_data = NULL;
   }
   return FAILURE;
}

// **************************************************************
// **************************************************************

// this function reads a 24-bit rgb subimage into the argument
// red, green, and blue pixel arrays
// returns SUCCESS or FAILURE

int CGeoTiffFrameFile::get_24bit_rgb_as_rgb_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array )
{
	int istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
	int ibyte, decompressed_size, num_remaining_bytes;
	int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
	unsigned int strip_offset, strip_byte_count;
	unsigned char red, green, blue, *strip_data, *decompressed_strip_data;
	int iwidth, iheight;

   //int kx, ky, inpos, outpos, rslt;

	unsigned char *jred, *jgrn, *jblu;

	int width, height, maxpos;
	std::string error;

	jred = jgrn = jblu = NULL;

	// find strip offsets tag
	if ( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
		goto FAIL;

	// find strip byte counts tag
	if ( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
		goto FAIL;

	width = max_hpix - min_hpix + 1;
	height = max_vpix - min_vpix + 1;

	iwidth = width;
	iheight = height;

	maxpos = width * height;

	// allocate data for strips
	strip_data = NULL;

	//   strip_data = new unsigned char[m_max_strip_byte_count];
	strip_data = new unsigned char[m_max_strip_byte_count];
   if (strip_data == nullptr)
      goto FAIL;

	decompressed_strip_data = NULL;

	if (m_compression_scheme == GEOTIFF_JPEG)
	{
		// alloc memory for jpeg tiffs
		jred = new unsigned char[width * m_rows_per_strip * 2];
      if (jred == nullptr)
         goto FAIL;
		jgrn = new unsigned char[width * m_rows_per_strip * 2];
      if (jgrn == nullptr)
         goto FAIL;
		jblu = new unsigned char[width * m_rows_per_strip * 2];
      if (jblu == nullptr)
         goto FAIL;
	}
	else if ( m_compression_scheme == GEOTIFF_PACKBITS ||
	          m_compression_scheme == GEOTIFF_LZW )
	{
		// if compressed, allocate memory for a decompressed strip
		decompressed_strip_data = new unsigned char[m_image_width * m_rows_per_strip *
																m_samples_per_pixel];
      if (decompressed_strip_data == nullptr)
         goto FAIL;
	}

	// determine starting and ending strips
	start_strip = min_vpix / m_rows_per_strip;
	end_strip = max_vpix / m_rows_per_strip;

	// determine starting pixel in strip
	hpix = 0;
	vpix = start_strip * m_rows_per_strip;
	ipixel = vpix * m_image_width + hpix;
	subimage_pixel_index = 0;

	// loop through each strip
	for ( istrip = start_strip; istrip <= end_strip; istrip++ )
	{
		// get the strip offset
		switch( m_tags[strip_offsets_tag_index].m_type )
		{
			case GEOTIFF_SHORT:
				strip_offset =
				m_tags[strip_offsets_tag_index].m_short_values[istrip];
				break;
			case GEOTIFF_LONG:
				strip_offset =
				m_tags[strip_offsets_tag_index].m_long_values[istrip];
				break;
			default:
				goto FAIL;
		}

		// get the strip byte count
		switch ( m_tags[strip_byte_counts_tag_index].m_type )
		{
			case GEOTIFF_SHORT:
				strip_byte_count =
				m_tags[strip_byte_counts_tag_index].m_short_values[istrip];
				break;
			case GEOTIFF_LONG:
				strip_byte_count =
				m_tags[strip_byte_counts_tag_index].m_long_values[istrip];
				break;
			default:
				goto FAIL;
		}

		// set the file pointer to the row offset
		if ( fseek( m_file_ptr, strip_offset, SEEK_SET ) != 0 )
			goto FAIL;

		if (m_compression_scheme == GEOTIFF_JPEG)
		{
//#pragma message("Temporarily removed JPEG support until ImageLib is ready")
/*
			int yoff, yrange;

         outpos = 0;

			width = iwidth;
			yoff = min_vpix % m_rows_per_strip;
			if (istrip > start_strip)
				yoff = 0;

			yrange = m_rows_per_strip - yoff;

			if (m_jpeg_table_len > 0)
				jpeg.set_jpeg_tables(m_jpeg_table, m_jpeg_table_len);

			rslt = jpeg.get_jpeg_bitstream_image(m_file_ptr, min_hpix, yoff, width, yrange,
											jred, jgrn, jblu, error);

			inpos = 0;
			for (ky=0; ky<yrange; ky++)
			{
				for (kx=0; kx < iwidth; kx++)
				{
					red_array[outpos] = jred[inpos];
					green_array[outpos] = jgrn[inpos];
					blue_array[outpos] = jblu[inpos];
					inpos++;
					outpos++;
					if (outpos >= maxpos)
						goto DONE;

				}
			}
         */
		}
		else
		{
			// something other than JPEG compression

			// read the strip
			if ( fread( strip_data, strip_byte_count, 1, m_file_ptr ) != 1 )
			  goto FAIL;

			// select planar configuration - either chunky (rgbrgbrgb) or
			// planar (rrrgggbbb) format
			switch( m_planar_configuration )
			{
			 case GEOTIFF_CHUNKY_FORMAT:
				switch( m_compression_scheme )
				{
				   case GEOTIFF_NO_COMPRESSION:
					  ibyte = 0;
					  // copy strip data to pixel array
					  while( ibyte < (int)strip_byte_count )
					  {
						 red = strip_data[ibyte];
						 ibyte++;
						 green = strip_data[ibyte];
						 ibyte++;
						 blue = strip_data[ibyte];
						 ibyte++;
						 if( hpix >= min_hpix && hpix <= max_hpix &&
							 vpix >= min_vpix && vpix <= max_vpix )
						 {
							red_array[subimage_pixel_index] = red;
							green_array[subimage_pixel_index] = green;
							blue_array[subimage_pixel_index] = blue;
							subimage_pixel_index++;
						 }
						 hpix++;
						 if( hpix == (int)m_image_width )
						 {
							hpix = 0;
							vpix++;
							if( vpix > max_vpix )
								goto DONE;
						 }
						 ipixel++;
					  }
					  break;
				   case GEOTIFF_CCITT_1D:
					  goto FAIL;                     // not yet supported
				   case GEOTIFF_LZW:                      // LZW compression scheme
				   case GEOTIFF_PACKBITS:            // PackBits compression scheme
					  // decompress data
					  if( m_compression_scheme == GEOTIFF_PACKBITS ||
					      m_compression_scheme == GEOTIFF_LZW )
					  {
						 num_remaining_bytes =
							(m_num_pixels - ipixel) * m_samples_per_pixel;
						 decompressed_size =
							__min( (int)(m_image_width * m_rows_per_strip *
							m_samples_per_pixel), num_remaining_bytes );
						 if( decompress_strip( (int)strip_byte_count, strip_data,
							 decompressed_size, decompressed_strip_data ) != SUCCESS )
							 goto FAIL;
					  }
					  ibyte = 0;
					  // copy strip data to pixel array
					  while( ibyte < (int)decompressed_size )
					  {
						 red = decompressed_strip_data[ibyte];
						 ibyte++;
						 green = decompressed_strip_data[ibyte];
						 ibyte++;
						 blue = decompressed_strip_data[ibyte];
						 ibyte++;
						 if( hpix >= min_hpix && hpix <= max_hpix &&
							 vpix >= min_vpix && vpix <= max_vpix )
						 {
							red_array[subimage_pixel_index] = red;
							green_array[subimage_pixel_index] = green;
							blue_array[subimage_pixel_index] = blue;
							subimage_pixel_index++;
						 }
						 hpix++;
						 if( hpix == (int)m_image_width )
						 {
							hpix = 0;
							vpix++;
							if( vpix > max_vpix )
								goto DONE;
						 }
						 ipixel++;
					  }
					  break;
				   default:
					  goto FAIL;
				}
				break;

			 case GEOTIFF_PLANAR_FORMAT:
				goto FAIL;                             // not yet supported

			 default:
				goto FAIL;
			}
		}
	}


DONE:
	if (strip_data != NULL)
		delete [] strip_data;
   strip_data = NULL;

   if (jred != NULL)
	   delete [] jred;
   if (jgrn != NULL)
	   delete [] jgrn;
   if (jblu != NULL)
	   delete [] jblu;

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      delete [] decompressed_strip_data;
      decompressed_strip_data = NULL;
   }

   return SUCCESS;

FAIL:
   if (jred != NULL)
	   delete [] jred;
   if (jgrn != NULL)
	   delete [] jgrn;
   if (jblu != NULL)
	   delete [] jblu;

   if( strip_data != NULL )
   {
      delete [] strip_data;
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      delete [] decompressed_strip_data;
      decompressed_strip_data = NULL;
   }
   return FAILURE;
}

// **************************************************************
// **************************************************************

int CGeoTiffFrameFile::get_8bit_palette_as_rgb_subimage( int min_hpix, int min_vpix,
      int max_hpix, int max_vpix, unsigned char *red_array,
      unsigned char *green_array, unsigned char *blue_array )
{
   // this function reads an 8-bit palette image into the argument
   // red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int istrip, strip_offsets_tag_index, strip_byte_counts_tag_index;
   int ibyte, decompressed_size, num_remaining_bytes;
   int hpix, vpix, ipixel, subimage_pixel_index, start_strip, end_strip;
   unsigned int strip_offset, strip_byte_count, palette_index;
   unsigned char *strip_data, *decompressed_strip_data;

   // find strip offsets tag
   if( find_tag( GEOTIFF_STRIP_OFFSETS_TAG, strip_offsets_tag_index ) != SUCCESS )
	   goto FAIL;

   // find strip byte counts tag
   if( find_tag( GEOTIFF_STRIP_BYTE_COUNTS_TAG, strip_byte_counts_tag_index ) != SUCCESS )
	   goto FAIL;

   // allocate data for strips
   strip_data = NULL;
   strip_data = new unsigned char[m_max_strip_byte_count];
   if (strip_data == nullptr)
      goto FAIL;

   // if compressed, allocate memory for a decompressed strip
   decompressed_strip_data = NULL;
   if( m_compression_scheme != GEOTIFF_NO_COMPRESSION )
   {
      decompressed_strip_data =
         new unsigned char[m_image_width * m_rows_per_strip *
                           m_samples_per_pixel];
      if (decompressed_strip_data == nullptr)
         goto FAIL;
   }

   // determine starting and ending strips
   start_strip = min_vpix / m_rows_per_strip;
   end_strip = max_vpix / m_rows_per_strip;

   // determine starting pixel in strip
   hpix = 0;
   vpix = start_strip * m_rows_per_strip;
   ipixel = vpix * m_image_width + hpix;
   subimage_pixel_index = 0;

   // loop through each strip
   for( istrip = start_strip; istrip <= end_strip; istrip++ )
   {
      // get the strip offset
      switch( m_tags[strip_offsets_tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            strip_offset =
               m_tags[strip_offsets_tag_index].m_short_values[istrip];
            break;
         case GEOTIFF_LONG:
            strip_offset =
               m_tags[strip_offsets_tag_index].m_long_values[istrip];
            break;
         default:
            goto FAIL;
      }
      // get the strip byte count
      switch( m_tags[strip_byte_counts_tag_index].m_type )
      {
         case GEOTIFF_SHORT:
            strip_byte_count =
               m_tags[strip_byte_counts_tag_index].m_short_values[istrip];
            break;
         case GEOTIFF_LONG:
            strip_byte_count =
               m_tags[strip_byte_counts_tag_index].m_long_values[istrip];
            break;
         default:
            goto FAIL;
      }

      // set the file pointer to the row offset
      if( fseek( m_file_ptr, strip_offset, SEEK_SET ) != 0 )
		  goto FAIL;

      // read the strip
      if( fread( strip_data, strip_byte_count, 1, m_file_ptr ) != 1 )
		  goto FAIL;

      // select planar configuration - either chunky (rgbrgbrgb) or
      // planar (rrrgggbbb) format
      switch( m_planar_configuration )
      {
         case GEOTIFF_CHUNKY_FORMAT:
            switch( m_compression_scheme )
            {
               case GEOTIFF_NO_COMPRESSION:
                  ibyte = 0;
                  while( ibyte < (int)strip_byte_count )
                  {
                     palette_index = strip_data[ibyte];
                     if( (int)palette_index >= m_num_color_map_values )
                        goto FAIL;
                     ibyte++;
                     if( hpix >= min_hpix && hpix <= max_hpix &&
                         vpix >= min_vpix && vpix <= max_vpix )
                     {
                        red_array[subimage_pixel_index] =
                           (unsigned char)(m_red_color_map[palette_index]/256);
                        green_array[subimage_pixel_index] =
                          (unsigned char)(m_green_color_map[palette_index]/256);
                        blue_array[subimage_pixel_index] =
                           (unsigned char)(m_blue_color_map[palette_index]/256);
                        subimage_pixel_index++;
                     }
                     hpix++;
                     if( hpix == (int)m_image_width )
                     {
                        hpix = 0;
                        vpix++;
                        if( vpix > max_vpix )
							goto DONE;
                     }
                     ipixel++;
                  }
                  break;
               case GEOTIFF_CCITT_1D:
                  goto FAIL;                      // not yet supported
               case GEOTIFF_LZW:                      // LZW compression scheme
               case GEOTIFF_PACKBITS:             // PackBits compression scheme
                  // decompress data
                  if( m_compression_scheme == GEOTIFF_PACKBITS ||
                      m_compression_scheme == GEOTIFF_LZW )
                  {
                     num_remaining_bytes =
                        (m_num_pixels - ipixel) * m_samples_per_pixel;
                     decompressed_size =
                        __min( (int)(m_image_width * m_rows_per_strip *
                        m_samples_per_pixel), num_remaining_bytes );
                     if( decompress_strip( (int)strip_byte_count, strip_data,
                         decompressed_size, decompressed_strip_data ) != SUCCESS )
						 goto FAIL;
                  }
                  ibyte = 0;
                  while( ibyte < decompressed_size )
                  {
                     palette_index = decompressed_strip_data[ibyte];
                     if( (int)palette_index >= m_num_color_map_values )
                        goto FAIL;
                     ibyte++;
                     if( hpix >= min_hpix && hpix <= max_hpix &&
                         vpix >= min_vpix && vpix <= max_vpix )
                     {
                        red_array[subimage_pixel_index] =
                           (unsigned char)(m_red_color_map[palette_index]/256);
                        green_array[subimage_pixel_index] =
                          (unsigned char)(m_green_color_map[palette_index]/256);
                        blue_array[subimage_pixel_index] =
                           (unsigned char)(m_blue_color_map[palette_index]/256);
                        subimage_pixel_index++;
                     }
                     hpix++;
                     if( hpix == (int)m_image_width )
                     {
                        hpix = 0;
                        vpix++;
                        if( vpix > max_vpix )
							goto DONE;
                     }
                     ipixel++;
                  }
                  break;
               default:
                  goto FAIL;
            }
            break;

         case GEOTIFF_PLANAR_FORMAT:
            goto FAIL;                                     // not yet supported

         default:
            goto FAIL;
      }

   }

DONE:
   delete [] strip_data;
   strip_data = NULL;

   // deallocate decompressed strip data array
   if( decompressed_strip_data != NULL )
   {
      delete [] decompressed_strip_data;
      decompressed_strip_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( strip_data != NULL )
   {
      delete [] strip_data;
      strip_data = NULL;
   }
   if( decompressed_strip_data != NULL )
   {
      delete [] decompressed_strip_data;
      decompressed_strip_data = NULL;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_tile_as_rgb_image( int itile,
   unsigned char *red_array, unsigned char *green_array,
   unsigned char *blue_array )
{
   // this function returns a tile as an rgb image
	int rslt;

   // select image type and call appropriate function
   switch( m_image_type )
   {
      case GEOTIFF_IMAGE_TYPE_NOT_SUPPORTED:
         return FAILURE;

      case GEOTIFF_IMAGE_TYPE_256_GRAYSCALE:
		  if (m_samples_per_pixel == 3)
			rslt = get_24bit_rgb_tile_as_rgb_image( itile, red_array, green_array, blue_array );
		  else
			rslt = get_8bit_grayscale_tile_as_rgb_image( itile, red_array, green_array, blue_array );
		  if (rslt != SUCCESS )
            return FAILURE;
         break;

      case GEOTIFF_IMAGE_TYPE_16BIT_GRAYSCALE:
         if( get_16bit_grayscale_tile_as_rgb_image( itile,
            red_array, green_array, blue_array ) != SUCCESS )
            return FAILURE;
         break;

      case GEOTIFF_IMAGE_TYPE_256_COLOR:
         if( get_8bit_palette_tile_as_rgb_image( itile,
            red_array, green_array, blue_array ) != SUCCESS )
            return FAILURE;
         break;

      case GEOTIFF_IMAGE_TYPE_24BIT_COLOR:
         if( get_24bit_rgb_tile_as_rgb_image( itile,
            red_array, green_array, blue_array ) != SUCCESS )
            return FAILURE;
         break;

         default:                           // should always be one of above
            return FAILURE;
   }

   return SUCCESS;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_8bit_grayscale_tile_as_rgb_image( int itile,
   unsigned char *red_array, unsigned char *green_array,
   unsigned char *blue_array )
{
   // this function reads a 8-bit grayscale image tile into the argument
   // red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i, tile_offsets_tag_index, tile_byte_counts_tag_index;
   int pixel_index, decompressed_size, num_remaining_bytes;
   unsigned int tile_offset, tile_byte_count;
   unsigned char *tile_data = NULL, *decompressed_tile_data = NULL;

   // check for tiled image
   if( !m_image_is_tiled )
      goto FAIL;

   // check for valid tile index
   if( itile < 0 || itile >= (int)m_num_tiles )
      goto FAIL;

   // find tile offsets tag
   if( find_tag( GEOTIFF_TILE_OFFSETS_TAG, tile_offsets_tag_index )
      != SUCCESS )
      goto FAIL;

   // find tile byte counts tag
   if( find_tag( GEOTIFF_TILE_BYTE_COUNTS_TAG, tile_byte_counts_tag_index )
      != SUCCESS )
      goto FAIL;

   // get the tile offset
   switch( m_tags[tile_offsets_tag_index].m_type )
   {
      case GEOTIFF_LONG:
         tile_offset =
            m_tags[tile_offsets_tag_index].m_long_values[itile];
         break;
      default:
         goto FAIL;
   }

   // get the tile byte count
   switch( m_tags[tile_byte_counts_tag_index].m_type )
   {
      case GEOTIFF_SHORT:
         tile_byte_count =
            m_tags[tile_byte_counts_tag_index].m_short_values[itile];
         break;
      case GEOTIFF_LONG:
         tile_byte_count =
            m_tags[tile_byte_counts_tag_index].m_long_values[itile];
         break;
      default:
         goto FAIL;
   }

   // allocate memory to hold the tile data
   tile_data = NULL;
//   tile_data = new unsigned char[tile_byte_count];
   tile_data = new unsigned char[tile_byte_count];
   if( tile_data == NULL )
      goto FAIL;

   // set the file pointer to the tile offset
   if( fseek( m_file_ptr, tile_offset, SEEK_SET ) != 0 )
      goto FAIL;

   // read the tile
   if( fread( tile_data, tile_byte_count, 1, m_file_ptr ) != 1 )
      goto FAIL;

   // check compression scheme
   switch( m_compression_scheme )
   {
      case GEOTIFF_NO_COMPRESSION:
         // copy tile data to pixel array
         pixel_index = 0;
         for( i = 0; i < (int)tile_byte_count; i++ )
         {
            red_array[pixel_index] = tile_data[i];
            pixel_index++;
         }
         break;
      case GEOTIFF_LZW:                      // LZW compression scheme
      case GEOTIFF_PACKBITS:                 // PackBits compression scheme
         // allocate memory for decompressed data
         decompressed_tile_data = NULL;
//          decompressed_strip_data =
//          new unsigned char[m_tile_width * m_tile_length];
         decompressed_tile_data =
            new unsigned char[m_tile_width * m_tile_length];
         if (decompressed_tile_data == nullptr)
            goto FAIL;
         num_remaining_bytes = m_tile_width * m_tile_length;
         decompressed_size = m_tile_width * m_tile_length;
         // decompress data
         if( decompress_strip( (int)tile_byte_count, tile_data,
             decompressed_size, decompressed_tile_data ) != SUCCESS )
             goto FAIL;
         // copy decompressed strip data to pixel array
         pixel_index = 0;
         for( i = 0; i < decompressed_size; i++ )
         {
            red_array[pixel_index] = decompressed_tile_data[i];
            pixel_index++;
         }
         break;
      default:
         goto FAIL;
   }

   // deallocate tile data array
   if( tile_data != NULL )
   {
      delete [] tile_data;
      tile_data = NULL;
   }

   // deallocate decompressed tile data array
   if( decompressed_tile_data != NULL )
   {
      delete [] decompressed_tile_data;
      decompressed_tile_data = NULL;
   }

   // check for reversed grayscale
   if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
   {
      for( i = 0; i < (int)(m_tile_width*m_tile_length); i++ )
         red_array[i] = 255 - red_array[i];
   }

   // copy red array data to green and blue arrays
   for( i = 0; i < (int)(m_tile_width*m_tile_length); i++ )
      green_array[i] = blue_array[i] = red_array[i];

   return SUCCESS;

FAIL:
   if( tile_data != NULL )
   {
      delete [] tile_data;
      tile_data = NULL;
   }
   if( decompressed_tile_data != NULL )
   {
      delete [] decompressed_tile_data;
      decompressed_tile_data = NULL;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_16bit_grayscale_tile_as_rgb_image( int itile,
   unsigned char *red_array, unsigned char *green_array,
   unsigned char *blue_array )
{
   // this function reads a 16-bit grayscale image tile into the argument
   // red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i, tile_offsets_tag_index, tile_byte_counts_tag_index;
   int pixel_index, decompressed_size, num_remaining_bytes;
   unsigned int tile_offset, tile_byte_count;
   unsigned char *tile_data = NULL;
   unsigned char *decompressed_tile_data = NULL;
   unsigned char byte1, byte2;
   unsigned short *tile_16bit_data;
   double fraction;

   // check for tiled image
   if( !m_image_is_tiled )
      goto FAIL;

   // check for valid tile index
   if( itile < 0 || itile >= (int)m_num_tiles )
      goto FAIL;

   // find tile offsets tag
   if( find_tag( GEOTIFF_TILE_OFFSETS_TAG, tile_offsets_tag_index )
      != SUCCESS )
      goto FAIL;

   // find tile byte counts tag
   if( find_tag( GEOTIFF_TILE_BYTE_COUNTS_TAG, tile_byte_counts_tag_index )
      != SUCCESS )
      goto FAIL;

   // get the tile offset
   switch( m_tags[tile_offsets_tag_index].m_type )
   {
      case GEOTIFF_LONG:
         tile_offset =
            m_tags[tile_offsets_tag_index].m_long_values[itile];
         break;
      default:
         goto FAIL;
   }

   // get the tile byte count
   switch( m_tags[tile_byte_counts_tag_index].m_type )
   {
      case GEOTIFF_SHORT:
         tile_byte_count =
            m_tags[tile_byte_counts_tag_index].m_short_values[itile];
         break;
      case GEOTIFF_LONG:
         tile_byte_count =
            m_tags[tile_byte_counts_tag_index].m_long_values[itile];
         break;
      default:
         goto FAIL;
   }

   // allocate memory to hold the tile data
   tile_data = NULL;
//   tile_data = new unsigned char[tile_byte_count];
   tile_data = new unsigned char[tile_byte_count];
   if( tile_data == NULL )
      goto FAIL;

   // set the file pointer to the tile offset
   if( fseek( m_file_ptr, tile_offset, SEEK_SET ) != 0 )
      goto FAIL;

   // read the tile
   if( fread( tile_data, tile_byte_count, 1, m_file_ptr ) != 1 )
      goto FAIL;

   // check compression scheme
   switch( m_compression_scheme )
   {
      case GEOTIFF_NO_COMPRESSION:
         // swap byte order if necessary
         if( m_byte_order == GEOTIFF_BIG_ENDIAN )
         {
            for( i = 0; i < (int)tile_byte_count; i += 2 )
            {
               byte1 = tile_data[i];
               byte2 = tile_data[i+1];
               tile_data[i] = byte2;
               tile_data[i+1] = byte1;
            }
         }
         // copy tile data to pixel array
         tile_16bit_data = (unsigned short*)tile_data;
         pixel_index = 0;
         for( i = 0; i < (int)tile_byte_count/2; i++ )
         {
            fraction = (double)(tile_16bit_data[i]-m_min_sample_value)/
               (m_max_sample_value-m_min_sample_value);
            if( fraction < 0.0 )
               fraction = 0.0;
            if( fraction > 1.0 )
               fraction = 1.0;
            red_array[pixel_index] = (unsigned char)(255*fraction+0.5);
            pixel_index++;
         }
         break;
      case GEOTIFF_LZW:                      // LZW compression scheme
      case GEOTIFF_PACKBITS:                 // PackBits compression scheme
         // allocate memory for decompressed data
         decompressed_tile_data = NULL;
//         decompressed_tile_data =
//         new unsigned char[2 * m_tile_width * m_tile_length];
         decompressed_tile_data =
            new unsigned char[2 * m_tile_width * m_tile_length];
         if( decompressed_tile_data == NULL )
            goto FAIL;
         num_remaining_bytes = 2 * m_tile_width * m_tile_length;
         decompressed_size = 2 * m_tile_width * m_tile_length;
         // decompress data
         if( decompress_strip( (int)tile_byte_count, tile_data,
             decompressed_size, decompressed_tile_data ) != SUCCESS )
             goto FAIL;
         // swap byte order if necessary
         if( m_byte_order == GEOTIFF_BIG_ENDIAN )
         {
            for( i = 0; i < (int)decompressed_size; i += 2 )
            {
               byte1 = decompressed_tile_data[i];
               byte2 = decompressed_tile_data[i+1];
               decompressed_tile_data[i] = byte2;
               decompressed_tile_data[i+1] = byte1;
            }
         }
         // copy decompressed strip data to pixel array
         tile_16bit_data = (unsigned short*)decompressed_tile_data;
         pixel_index = 0;
         for( i = 0; i < decompressed_size/2; i++ )
         {
            fraction = (double)(tile_16bit_data[i]-m_min_sample_value)/
               (m_max_sample_value-m_min_sample_value);
            if( fraction < 0.0 )
               fraction = 0.0;
            if( fraction > 1.0 )
               fraction = 1.0;
            red_array[pixel_index] = (unsigned char)(255*fraction+0.5);
            pixel_index++;
         }
         break;
      default:
         goto FAIL;
   }

   // deallocate tile data array
   if( tile_data != NULL )
   {
      delete [] tile_data;
      tile_data = NULL;
   }

   // deallocate decompressed tile data array
   if( decompressed_tile_data != NULL )
   {
      delete [] decompressed_tile_data;
      decompressed_tile_data = NULL;
   }

   // check for reversed grayscale
   if( m_photometric_interpretation == GEOTIFF_WHITE_IS_ZERO )
   {
      for( i = 0; i < (int)(m_tile_width*m_tile_length); i++ )
         red_array[i] = 255 - red_array[i];
   }

   // copy red array data to green and blue arrays
   for( i = 0; i < (int)(m_tile_width*m_tile_length); i++ )
      green_array[i] = blue_array[i] = red_array[i];

   return SUCCESS;

FAIL:
   if( tile_data != NULL )
   {
      delete [] tile_data;
      tile_data = NULL;
   }
   if( decompressed_tile_data != NULL )
   {
      delete [] decompressed_tile_data;
      decompressed_tile_data = NULL;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_24bit_rgb_tile_as_rgb_image( int itile,
   unsigned char *red_array, unsigned char *green_array,
   unsigned char *blue_array )
{
   // this function reads a tiled 24-bit rgb image tile into the argument
   // red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i, tile_offsets_tag_index, tile_byte_counts_tag_index;
   int pixel_index, decompressed_size, num_remaining_bytes;
   unsigned int tile_offset, tile_byte_count;
   unsigned char *tile_data = NULL, *decompressed_tile_data = NULL;
   std::string error_msg;

   //CJpeg jpeg;
   //int rslt, width, height;

   // check for tiled image
   if( !m_image_is_tiled )
      goto FAIL;

   // check for valid tile index
   if( itile < 0 || itile >= (int)m_num_tiles )
      goto FAIL;

   // find tile offsets tag
   if( find_tag( GEOTIFF_TILE_OFFSETS_TAG, tile_offsets_tag_index ) != SUCCESS )
      goto FAIL;

   // find tile byte counts tag
   if( find_tag( GEOTIFF_TILE_BYTE_COUNTS_TAG, tile_byte_counts_tag_index )
      != SUCCESS )
      goto FAIL;

   // get the tile offset
   switch( m_tags[tile_offsets_tag_index].m_type )
   {
      case GEOTIFF_LONG:
         tile_offset =
            m_tags[tile_offsets_tag_index].m_long_values[itile];
         break;
      default:
         goto FAIL;
   }

   // get the tile byte count
   switch( m_tags[tile_byte_counts_tag_index].m_type )
   {
      case GEOTIFF_SHORT:
         tile_byte_count =
            m_tags[tile_byte_counts_tag_index].m_short_values[itile];
         break;
      case GEOTIFF_LONG:
         tile_byte_count =
            m_tags[tile_byte_counts_tag_index].m_long_values[itile];
         break;
      default:
         goto FAIL;
   }

   // allocate memory to hold the tile data
   tile_data = NULL;
   tile_data = new unsigned char[tile_byte_count];
   if( tile_data == NULL )
      goto FAIL;

   // set the file pointer to the tile offset
   if( fseek( m_file_ptr, tile_offset, SEEK_SET ) != 0 )
      goto FAIL;

   // read the tile
   if( fread( tile_data, tile_byte_count, 1, m_file_ptr ) != 1 )
      goto FAIL;

   // check compression scheme
   switch( m_compression_scheme )
   {
      case GEOTIFF_NO_COMPRESSION:
         // copy tile data to pixel array
         pixel_index = 0;
         for( i = 0; i < (int)tile_byte_count; i+=3 )
         {
            red_array[pixel_index] = tile_data[i];
            green_array[pixel_index] = tile_data[i+1];
            blue_array[pixel_index] = tile_data[i+2];
            pixel_index++;
         }
         break;
      case GEOTIFF_LZW:                      // LZW compression scheme
      case GEOTIFF_PACKBITS:                 // PackBits compression scheme
         // allocate memory for decompressed data
         decompressed_tile_data = NULL;
         decompressed_tile_data =
            new unsigned char[3 * m_tile_width * m_tile_length];
         if (decompressed_tile_data == nullptr)
            goto FAIL;
         num_remaining_bytes = 3 * m_tile_width * m_tile_length;
         decompressed_size = 3 * m_tile_width * m_tile_length;
         // decompress data
         if( decompress_strip( (int)tile_byte_count, tile_data,
             decompressed_size, decompressed_tile_data ) != SUCCESS )
             goto FAIL;
         // copy decompressed strip data to pixel array
         pixel_index = 0;
         for( i = 0; i < decompressed_size; i+= 3 )
         {
            red_array[pixel_index] = decompressed_tile_data[i];
            green_array[pixel_index] = decompressed_tile_data[i+1];
            blue_array[pixel_index] = decompressed_tile_data[i+2];
            pixel_index++;
         }
         break;
      case GEOTIFF_JPEG:                 // JPGGEOTIFF_JPEG compression scheme
//#pragma message("Temporarily removed JPEG support until ImageLib is ready")
         /*
		   // set the file pointer to the tile offset
		   if( fseek( m_file_ptr, tile_offset, SEEK_SET ) != 0 )
			  goto FAIL;

			if (m_jpeg_table_len > 0)
				jpeg.set_jpeg_tables(m_jpeg_table, m_jpeg_table_len);

			width = m_tile_width;
			height = m_tile_length;
//			rslt = jpeg.check_jpeg_file(m_file_ptr, error_msg);
//			if (rslt != SUCCESS)
//				AfxMessageBox("Found bad Jpeg stream");
			rslt = jpeg.get_jpeg_bitstream_image(m_file_ptr, 0, 0, width, height,
											red_array, green_array, blue_array, error_msg);
                                 */
		 break;
      default:
         goto FAIL;
   }

   // deallocate tile data array
   if( tile_data != NULL )
   {
      delete [] tile_data;
      tile_data = NULL;
   }

   // deallocate decompressed tile data array
   if( decompressed_tile_data != NULL )
   {
      delete [] decompressed_tile_data;
      decompressed_tile_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( tile_data != NULL )
   {
      delete [] tile_data;
      tile_data = NULL;
   }
   if( decompressed_tile_data != NULL )
   {
      delete [] decompressed_tile_data;
      decompressed_tile_data = NULL;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::get_8bit_palette_tile_as_rgb_image( int itile,
   unsigned char *red_array, unsigned char *green_array,
   unsigned char *blue_array )
{
   // this function reads a 8-bit palette image tile into the argument
   // red, green, and blue pixel arrays
   // returns SUCCESS or FAILURE

   int i, tile_offsets_tag_index, tile_byte_counts_tag_index;
   int decompressed_size, num_remaining_bytes;
   int palette_index;
   unsigned int tile_offset, tile_byte_count;
   unsigned char *tile_data = NULL, *decompressed_tile_data = NULL;

   // check for tiled image
   if( !m_image_is_tiled )
      goto FAIL;

   // check for valid tile index
   if( itile < 0 || itile >= (int)m_num_tiles )
      goto FAIL;

   // find tile offsets tag
   if( find_tag( GEOTIFF_TILE_OFFSETS_TAG, tile_offsets_tag_index )
      != SUCCESS )
      goto FAIL;

   // find tile byte counts tag
   if( find_tag( GEOTIFF_TILE_BYTE_COUNTS_TAG, tile_byte_counts_tag_index )
      != SUCCESS )
      goto FAIL;

   // get the tile offset
   switch( m_tags[tile_offsets_tag_index].m_type )
   {
      case GEOTIFF_LONG:
         tile_offset =
            m_tags[tile_offsets_tag_index].m_long_values[itile];
         break;
      default:
         goto FAIL;
   }

   // get the tile byte count
   switch( m_tags[tile_byte_counts_tag_index].m_type )
   {
      case GEOTIFF_SHORT:
         tile_byte_count =
            m_tags[tile_byte_counts_tag_index].m_short_values[itile];
         break;
      case GEOTIFF_LONG:
         tile_byte_count =
            m_tags[tile_byte_counts_tag_index].m_long_values[itile];
         break;
      default:
         goto FAIL;
   }

   // allocate memory to hold the tile data
   tile_data = NULL;
//   tile_data = new unsigned char[tile_byte_count];
   tile_data = new unsigned char[tile_byte_count];
   if( tile_data == NULL )
      goto FAIL;

   // set the file pointer to the tile offset
   if( fseek( m_file_ptr, tile_offset, SEEK_SET ) != 0 )
      goto FAIL;

   // read the tile
   if( fread( tile_data, tile_byte_count, 1, m_file_ptr ) != 1 )
      goto FAIL;

   // check compression scheme
   switch( m_compression_scheme )
   {
      case GEOTIFF_NO_COMPRESSION:
         // copy tile data to pixel array
         for( i = 0; i < (int)tile_byte_count; i++ )
         {
            palette_index = tile_data[i];
            if( (int)palette_index >= m_num_color_map_values )
               goto FAIL;
            red_array[i] =
               (unsigned char)(m_red_color_map[palette_index]/256);
            green_array[i] =
               (unsigned char)(m_green_color_map[palette_index]/256);
            blue_array[i] =
               (unsigned char)(m_blue_color_map[palette_index]/256);
         }
         break;
      case GEOTIFF_LZW:                      // LZW compression scheme
      case GEOTIFF_PACKBITS:                 // PackBits compression scheme
         // allocate memory for decompressed data
         decompressed_tile_data = NULL;
//          decompressed_strip_data =
//          new unsigned char[m_tile_width * m_tile_length];
         decompressed_tile_data =
            new unsigned char[m_tile_width * m_tile_length];
         if (decompressed_tile_data == nullptr)
            goto FAIL;
         num_remaining_bytes = m_tile_width * m_tile_length;
         decompressed_size = m_tile_width * m_tile_length;
         // decompress data
         if( decompress_strip( (int)tile_byte_count, tile_data,
             decompressed_size, decompressed_tile_data ) != SUCCESS )
             goto FAIL;
         // copy decompressed strip data to pixel array
         for( i = 0; i < (int)m_num_tile_pixels; i++ )
         {
            palette_index = decompressed_tile_data[i];
            if( (int)palette_index >= m_num_color_map_values )
               goto FAIL;
            red_array[i] =
               (unsigned char)(m_red_color_map[palette_index]/256);
            green_array[i] =
               (unsigned char)(m_green_color_map[palette_index]/256);
            blue_array[i] =
               (unsigned char)(m_blue_color_map[palette_index]/256);
         }
         break;
      default:
         goto FAIL;
   }

   // deallocate tile data array
   if( tile_data != NULL )
   {
      delete [] tile_data;
      tile_data = NULL;
   }

   // deallocate decompressed tile data array
   if( decompressed_tile_data != NULL )
   {
      delete [] decompressed_tile_data;
      decompressed_tile_data = NULL;
   }

   return SUCCESS;

FAIL:
   if( tile_data != NULL )
   {
      delete [] tile_data;
      tile_data = NULL;
   }
   if( decompressed_tile_data != NULL )
   {
      delete [] decompressed_tile_data;
      decompressed_tile_data = NULL;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::read_string( char *string, int length )
{
   // reads a string of specified length into the specified buffer
   // terminating character is converted from '|' to a null
   // returns SUCCESS or FAILURE
   int i;

   if( fread( string, 1, length, m_file_ptr ) == (unsigned)length )
   {
      for( i = length-1; i >= 0; i-- )
      {
         if( string[i] == '|' )
         {
            string[i] = NULL;
            break;
         }
      }
      // guarantee null terminated
      string[length-1] = NULL;
      return SUCCESS;
   }
   else
      return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::read_signed_short( short &value )
{
   // reads a signed short into the argument
   // returns SUCCESS or FAILURE

   switch( m_byte_order )
   {
      case GEOTIFF_BYTE_ORDER_NOT_DEFINED:
         return FAILURE;

      case GEOTIFF_LITTLE_ENDIAN:
         // read two bytes straight into argument
         if( fread( &value, 2, 1, m_file_ptr ) == 1 )
            return SUCCESS;
         else
            return FAILURE;

      case GEOTIFF_BIG_ENDIAN:
         // reverse byte order
         if( fread( &value, 2, 1, m_file_ptr ) != 1 )
			 return FAILURE;
         reverse_byte_order( (char*)&value, 2 );
         return SUCCESS;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::read_unsigned_short( unsigned short &value )
{
   // reads an unshort into the argument
   // returns SUCCESS or FAILURE

   switch( m_byte_order )
   {
      case GEOTIFF_BYTE_ORDER_NOT_DEFINED:
         return FAILURE;

      case GEOTIFF_LITTLE_ENDIAN:
         // read two bytes straight into argument
         if( fread( &value, 2, 1, m_file_ptr ) == 1 )
            return SUCCESS;
         else
            return FAILURE;

      case GEOTIFF_BIG_ENDIAN:
         // reverse byte order
         if( fread( &value, 2, 1, m_file_ptr ) != 1 )
			 return FAILURE;
         reverse_byte_order( (char*)&value, 2 );
         return SUCCESS;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::read_signed_int( int &value )
{
   // reads a signed integer into the argument
   // returns SUCCESS or FAILURE

   switch( m_byte_order )
   {
      case GEOTIFF_BYTE_ORDER_NOT_DEFINED:
         return FAILURE;

      case GEOTIFF_LITTLE_ENDIAN:
         // read four bytes straight into argument
         if( fread( &value, 4, 1, m_file_ptr ) == 1 )
            return SUCCESS;
         else
            return FAILURE;

      case GEOTIFF_BIG_ENDIAN:
         // reverse byte order
         if( fread( &value, 4, 1, m_file_ptr ) != 1 )
			 return FAILURE;
         reverse_byte_order( (char*)&value, 4 );
         return SUCCESS;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::read_unsigned_int( unsigned int &value )
{
   // reads an unsigned integer into the argument
   // returns SUCCESS or FAILURE

   switch( m_byte_order )
   {
      case GEOTIFF_BYTE_ORDER_NOT_DEFINED:
         return FAILURE;

      case GEOTIFF_LITTLE_ENDIAN:
         // read four bytes straight into argument
         if( fread( &value, 4, 1, m_file_ptr ) == 1 )
            return SUCCESS;
         else
            return FAILURE;

      case GEOTIFF_BIG_ENDIAN:
         // reverse byte order
         if( fread( &value, 4, 1, m_file_ptr ) != 1 )
			 return FAILURE;
         reverse_byte_order( (char*)&value, 4 );
         return SUCCESS;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::read_float( float &value )
{
   // reads a float into the argument
   // returns SUCCESS or FAILURE

   switch( m_byte_order )
   {
      case GEOTIFF_BYTE_ORDER_NOT_DEFINED:
         return FAILURE;

      case GEOTIFF_LITTLE_ENDIAN:
         // read four bytes straight into argument
         if( fread( &value, 4, 1, m_file_ptr ) == 1 )
            return SUCCESS;
         else
            return FAILURE;

      case GEOTIFF_BIG_ENDIAN:
         // reverse byte order
         if( fread( &value, 4, 1, m_file_ptr ) != 1 )
			 return FAILURE;
         reverse_byte_order( (char*)&value, 4 );
         return SUCCESS;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int CGeoTiffFrameFile::read_double( double &value )
{
   // reads a double into the argument
   // returns SUCCESS or FAILURE

   switch( m_byte_order )
   {
      case GEOTIFF_BYTE_ORDER_NOT_DEFINED:
         return FAILURE;

      case GEOTIFF_LITTLE_ENDIAN:
         // read four bytes straight into argument
         if( fread( &value, 8, 1, m_file_ptr ) == 1 )
            return SUCCESS;
         else
            return FAILURE;

      case GEOTIFF_BIG_ENDIAN:
         // reverse byte order
         if( fread( &value, 8, 1, m_file_ptr ) != 1 )
			 return FAILURE;
         reverse_byte_order( (char*)&value, 8 );
         return SUCCESS;
  }

   return FAILURE;
}

// *****************************************************************
// *****************************************************************

void CGeoTiffFrameFile::reverse_byte_order( char *buffer, int num_bytes )
{
   // this function reverses the byte order for the buffer argument
   // num_bytes specifies how many bytes there are in the buffer

   int i, j;
   char temp;

   for( i = 0, j = num_bytes-1; i < num_bytes/2; i++, j-- )
   {
      temp = buffer[i];
      buffer[i] = buffer[j];
      buffer[j] = temp;
   }
}

// *****************************************************************
// *****************************************************************

std::string CGeoTiffFrameFile::get_tag_name( unsigned short tag_id )
{
   // returns the name of the tag with id tag_id

   std::string tag_name;

   switch( tag_id )
   {
      case GEOTIFF_NEW_SUBFILE_TYPE_TAG:
         tag_name = "NewSubfileType";
         break;
      case GEOTIFF_SUBFILE_TYPE_TAG:
         tag_name = "SubfileType";
         break;
      case GEOTIFF_IMAGE_WIDTH_TAG:
         tag_name = "ImageWidth";
         break;
      case GEOTIFF_IMAGE_LENGTH_TAG:
         tag_name = "ImageLength";
         break;
      case GEOTIFF_BITS_PER_SAMPLE_TAG:
         tag_name = "BitsPerSample";
         break;
      case GEOTIFF_COMPRESSION_TAG:
         tag_name = "Compression";
         break;
      case GEOTIFF_PHOTOMETRIC_INTERPRETATION_TAG:
         tag_name = "PhotometricInterpretation";
         break;
      case GEOTIFF_THRESHHOLDING_TAG:
         tag_name = "Threshholding";
         break;
      case GEOTIFF_CELL_WIDTH_TAG:
         tag_name = "CellWidth";
         break;
      case GEOTIFF_CELL_LENGTH_TAG:
         tag_name = "CellLength";
         break;
      case GEOTIFF_FILL_ORDER_TAG:
         tag_name = "FillOrder";
         break;
      case GEOTIFF_DOCUMENT_NAME_TAG:
         tag_name = "DocumentName";
         break;
      case GEOTIFF_IMAGE_DESCRIPTION_TAG:
         tag_name = "ImageDescription";
         break;
      case GEOTIFF_MAKE_TAG:
         tag_name = "Make";
         break;
      case GEOTIFF_MODEL_TAG:
         tag_name = "Model";
         break;
      case GEOTIFF_STRIP_OFFSETS_TAG:
         tag_name = "StripOffsets";
         break;
      case GEOTIFF_ORIENTATION_TAG:
         tag_name = "Orientation";
         break;
      case GEOTIFF_SAMPLES_PER_PIXEL_TAG:
         tag_name = "SamplesPerPixel";
         break;
      case GEOTIFF_ROWS_PER_STRIP_TAG:
         tag_name = "RowsPerStrip";
         break;
      case GEOTIFF_STRIP_BYTE_COUNTS_TAG:
         tag_name = "StripByteCounts";
         break;
      case GEOTIFF_MIN_SAMPLE_VALUE_TAG:
         tag_name = "MinSampleValue";
         break;
      case GEOTIFF_MAX_SAMPLE_VALUE_TAG:
         tag_name = "MaxSampleValue";
         break;
      case GEOTIFF_X_RESOLUTION_TAG:
         tag_name = "XResolution";
         break;
      case GEOTIFF_Y_RESOLUTION_TAG:
         tag_name = "YResolution";
         break;
      case GEOTIFF_PLANAR_CONFIGURATION_TAG:
         tag_name = "PlanarConfiguration";
         break;
      case GEOTIFF_PAGE_NAME_TAG:
         tag_name = "PageName";
         break;
      case GEOTIFF_X_POSITION_TAG:
         tag_name = "XPosition";
         break;
      case GEOTIFF_Y_POSITION_TAG:
         tag_name = "YPosition";
         break;
      case GEOTIFF_FREE_OFFSETS_TAG:
         tag_name = "FreeOffsets";
         break;
      case GEOTIFF_FREE_BYTE_COUNTS_TAG:
         tag_name = "FreeByteCounts";
         break;
      case GEOTIFF_GRAY_RESPONSE_UNIT_TAG:
         tag_name = "GrayResponseUnit";
         break;
      case GEOTIFF_GRAY_RESPONSE_CURVE_TAG:
         tag_name = "GrayResponseCurve";
         break;
      case GEOTIFF_T4_OPTIONS_TAG:
         tag_name = "T4Options";
         break;
      case GEOTIFF_T6_OPTIONS_TAG:
         tag_name = "T6Options";
         break;
      case GEOTIFF_RESOLUTION_UNIT_TAG:
         tag_name = "ResolutionUnit";
         break;
      case GEOTIFF_PAGE_NUMBER_TAG:
         tag_name = "PageNumber";
         break;
      case GEOTIFF_TRANSFER_FUNCTION_TAG:
         tag_name = "TransferFunction";
         break;
      case GEOTIFF_SOFTWARE_TAG:
         tag_name = "Software";
         break;
      case GEOTIFF_DATE_TIME_TAG:
         tag_name = "DateTime";
         break;
      case GEOTIFF_ARTIST_TAG:
         tag_name = "Artist";
         break;
      case GEOTIFF_HOST_COMPUTER_TAG:
         tag_name = "HostComputer";
         break;
      case GEOTIFF_PREDICTOR_TAG:
         tag_name = "Predictor";
         break;
      case GEOTIFF_WHITE_POINT_TAG:
         tag_name = "WhitePoint";
         break;
      case GEOTIFF_PRIMARY_CHROMATICITIES_TAG:
         tag_name = "PrimaryChromaticities";
         break;
      case GEOTIFF_COLOR_MAP_TAG:
         tag_name = "ColorMap";
         break;
      case GEOTIFF_HALFTONE_HINTS_TAG:
         tag_name = "HalftoneHints";
         break;
      case GEOTIFF_TILE_WIDTH_TAG:
         tag_name = "TileWidth";
         break;
      case GEOTIFF_TILE_LENGTH_TAG:
         tag_name = "TileLength";
         break;
      case GEOTIFF_TILE_OFFSETS_TAG:
         tag_name = "TileOffsets";
         break;
      case GEOTIFF_TILE_BYTE_COUNTS_TAG:
         tag_name = "TileByteCounts";
         break;
      case GEOTIFF_INK_SET_TAG:
         tag_name = "InkSet";
         break;
      case GEOTIFF_INK_NAMES_TAG:
         tag_name = "InkNames";
         break;
      case GEOTIFF_NUMBER_OF_INKS_TAG:
         tag_name = "NumberOfInks";
         break;
      case GEOTIFF_DOT_RANGE_TAG:
         tag_name = "DotRange";
         break;
      case GEOTIFF_TARGET_PRINTER_TAG:
         tag_name = "TargetPrinter";
         break;
      case GEOTIFF_EXTRA_SAMPLES_TAG:
         tag_name = "ExtraSamples";
         break;
      case GEOTIFF_SAMPLE_FORMAT_TAG:
         tag_name = "SampleFormat";
         break;
      case GEOTIFF_S_MIN_SAMPLE_VALUE_TAG:
         tag_name = "SMinSampleValue";
         break;
      case GEOTIFF_S_MAX_SAMPLE_VALUE_TAG:
         tag_name = "SMaxSampleValue";
         break;
      case GEOTIFF_TRANSFER_RANGE_TAG:
         tag_name = "TransferRange";
         break;
      case GEOTIFF_JPEG_TABLES_TAG:
         tag_name = "JPEGTables";
         break;
      case GEOTIFF_JPEG_PROC_TAG:
         tag_name = "JPEGProc";
         break;
      case GEOTIFF_JPEG_INTERCHANGE_FORMAT_TAG:
         tag_name = "JPEGInterchangeFormat";
         break;
      case GEOTIFF_JPEG_INTERCHANGE_FORMAT_LNGTH_TAG:
         tag_name = "JPEGInterchangeFormatLngth";
         break;
      case GEOTIFF_JPEG_RESTART_INTERVAL_TAG:
         tag_name = "JPEGRestartInterval";
         break;
      case GEOTIFF_JPEG_LOSSLESS_PREDICTORS_TAG:
         tag_name = "JPEGLosslessPredictors";
         break;
      case GEOTIFF_JPEG_POINT_TRANSFORMS_TAG:
         tag_name = "JPEGPointTransforms";
         break;
      case GEOTIFF_JPEG_Q_TABLES_TAG:
         tag_name = "JPEGQTables";
         break;
      case GEOTIFF_JPEG_DC_TABLES_TAG:
         tag_name = "JPEGDCTables";
         break;
      case GEOTIFF_JPEC_AC_TABLES_TAG:
         tag_name = "JPEGACTables";
         break;
      case GEOTIFF_YCBCR_COEFFICIENTS_TAG:
         tag_name = "YCbCrCoefficients";
         break;
      case GEOTIFF_YCBCR_SUB_SAMPLING_TAG:
         tag_name = "YCbCrSubSampling";
         break;
      case GEOTIFF_YCBCR_POSITIONING_TAG:
         tag_name = "YCbCrPositioning";
         break;
      case GEOTIFF_REFERENCE_BLACK_WHITE_TAG:
         tag_name = "ReferenceBlackWhite";
         break;
      case GEOTIFF_COPYRIGHT_TAG:
         tag_name = "Copyright";
         break;
      case GEOTIFF_MODEL_PIXEL_SCALE_TAG:
         tag_name = "ModelPixelScaleTag";
         break;
      case GEOTIFF_MODEL_TRANSFORMATION_TAG:
         tag_name = "ModelTransformationTag";
         break;
      case GEOTIFF_MODEL_TIEPOINT_TAG:
         tag_name = "ModelTiepointTag";
         break;
      case GEOTIFF_GEOKEY_DIRECTORY_TAG :
         tag_name = "GeoKeyDirectoryTag";
         break;
      case GEOTIFF_GEO_DOUBLE_PARAMS_TAG:
         tag_name = "GeoDoubleParamsTag";
         break;
      case GEOTIFF_GEO_ASCII_PARAMS_TAG:
         tag_name = "GeoAsciiParamsTag";
         break;
      case GEOTIFF_INTERGRAPH_MATRIX_TAG:
         tag_name = "IntergraphMatrixTag";
         break;
      default:
         tag_name = "Unknown";
         break;
   }

   return tag_name;
}

// *****************************************************************
// *****************************************************************

std::string CGeoTiffFrameFile::get_type_name( unsigned short type )
{
   // returns the name of the specified tag data type

   std::string type_name;

   switch( type )
   {
      case GEOTIFF_BYTE:              // 8-bit unsigned integer
         type_name = "BYTE";
         break;
      case GEOTIFF_ASCII:             // null-terminated string
         type_name = "ASCII";
         break;
      case GEOTIFF_SHORT:             // unsigned short (16-bit) integer
         type_name = "SHORT";
        break;
      case GEOTIFF_LONG:              // unsigned long (32-bit) integer
         type_name = "LONG";
         break;
      case GEOTIFF_RATIONAL:          // num/denom pair of unsigned longs
         type_name = "RATIONAL";
         break;
      case GEOTIFF_SBYTE:             // 8-bit signed integer
         type_name = "SBYTE";
         break;
      case GEOTIFF_UNDEFINED_TYPE:         // 8-bit undefined data type
         type_name = "UNDEFINED TYPE";
         break;
      case GEOTIFF_SSHORT:            // signed short (16-bit) integer
         type_name = "SSHORT";
         break;
      case GEOTIFF_SLONG:             // signed long (32-bit) integer
         type_name = "SLONG";
         break;
      case GEOTIFF_SRATIONAL:         // num/denom pair of signed longs
         type_name = "SRATIONAL";
         break;
      case GEOTIFF_FLOAT:             // 4-byte float value
         type_name = "FLOAT";
         break;
      case GEOTIFF_DOUBLE:            // 8-byte double value
         type_name = "DOUBLE";
         break;
      default:
         type_name = "UNKNOWN";
         break;
   }

   return type_name;
}

// *****************************************************************
// *****************************************************************

std::string CGeoTiffFrameFile::get_tag_value_name( const CTiffTag &tag )
{
   // returns the name of the tag's value if it has a name
   // or it's formatted numeric values otherwise

   unsigned short i, imax;
   const int STRING_LEN = 100;
   char string[STRING_LEN];
   std::string value_name;

   value_name = "";
   switch( tag.m_tag_id )
   {
      case GEOTIFF_SUBFILE_TYPE_TAG:         // SubfileType
		  if (tag.m_short_values == NULL)  // sanity check
			  return value_name;
         switch( tag.m_short_values[0] )
         {
            case 1:
               value_name = "Full-Resolution Image";
               break;
            case 2:
               value_name = "Reduced-Resolution Image";
               break;
            case 3:
               value_name = "Single Page of Multi-Page Document";
               break;
            default:
               value_name = "Unknown";
               break;
         }
         break;

      case GEOTIFF_COMPRESSION_TAG:           // Compression
		  if (tag.m_short_values == NULL)  // sanity check
			  return value_name;
         switch( tag.m_short_values[0] )
         {
            case 1:
               value_name = "Uncompressed";
               break;
            case 2:
               value_name = "CCITT 1D";
               break;
            case 3:
               value_name = "Group 3 Fax";
               break;
            case 4:
               value_name = "Group 4 Fax";
               break;
            case 5:
               value_name = "LZW";
               break;
            case 6:
               value_name = "JPEG";
               break;
            case 32773:
               value_name = "PackBits";
               break;
            default:
               value_name = "Unknown";
               break;
         }
         break;

      case GEOTIFF_PHOTOMETRIC_INTERPRETATION_TAG:  // PhotometricInterpretation
		  if (tag.m_short_values == NULL)  // sanity check
			  return value_name;
         switch( tag.m_short_values[0] )
         {
            case 0:
               value_name = "WhiteIsZero";
               break;
            case 1:
               value_name = "BlackIsZero";
               break;
            case 2:
               value_name = "RGB";
               break;
            case 3:
               value_name = "RGB Palette";
               break;
            case 4:
               value_name = "Transparency Mask";
               break;
            case 5:
               value_name = "CMYK";
               break;
            case 6:
               value_name = "YCbCr";
               break;
            case 7:
               value_name = "CIELab";
               break;
            default:
               value_name = "Unknown";
               break;
         }
         break;

      case GEOTIFF_THRESHHOLDING_TAG:         // Threshholding
		  if (tag.m_short_values == NULL)  // sanity check
			  return value_name;
         switch( tag.m_short_values[0] )
         {
            case 1:
               value_name = "No Dithering or Halftoning Has Been Applied";
               break;
            case 2:
               value_name = "An Ordered Dither or Halftone Has Been Applied";
               break;
            case 3:
               value_name =
               "A Randomized Process Such As Error Diffusion Has Been Applied";
               break;
            default:
               value_name = "Unknown";
               break;
         }
         break;

      case GEOTIFF_PLANAR_CONFIGURATION_TAG:   // PlanarConfiguration
		  if (tag.m_short_values == NULL)  // sanity check
			  return value_name;
         switch( tag.m_short_values[0] )
         {
            case 1:
               value_name = "Chunky Format";
               break;
            case 2:
               value_name = "Planar Format";
               break;
            default:
               value_name = "Unknown";
               break;
         }
         break;

      case GEOTIFF_RESOLUTION_UNIT_TAG:       // ResolutionUnit
		  if (tag.m_short_values == NULL)  // sanity check
			  return value_name;
         switch( tag.m_short_values[0] )
         {
            case 1:
               value_name = "None";
               break;
            case 2:
               value_name = "Inch";
               break;
            case 3:
               value_name = "Centimeter";
               break;
            default:
               value_name = "Unknown";
               break;
         }
         break;

      default:                            // format data values
         switch( tag.m_type )
         {
            case GEOTIFF_BYTE:              // 8-bit unsigned integer
               value_name = "";
               imax = __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STRING_LEN, "%u  ", tag.m_byte_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count ) value_name += "...";
               break;
            case GEOTIFF_ASCII:             // null-terminated string
               value_name = "\"";
               value_name += tag.m_ascii_values;
               value_name += "\"";
               break;
            case GEOTIFF_SHORT:             // unsigned short (16-bit) integer
               value_name = "";
               imax = __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STRING_LEN, "%u  ", tag.m_short_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count ) value_name += "...";
              break;
            case GEOTIFF_LONG:              // unsigned long (32-bit) integer
               value_name = "";
               imax = __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STRING_LEN, "%i  ", tag.m_long_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count ) value_name += "...";
               break;
            case GEOTIFF_RATIONAL:          // num/denom pair of unsigned longs
               value_name = "";
               imax = __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STRING_LEN, "%lf  ", tag.m_double_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count ) value_name += "...";
               break;
            case GEOTIFF_SBYTE:             // 8-bit signed integer
               value_name = "";
               imax = __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STRING_LEN, "%i  ", tag.m_sbyte_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count ) value_name += "...";
               break;
            case GEOTIFF_UNDEFINED_TYPE:         // 8-bit undefined data type
               value_name = "";
               imax = __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STRING_LEN, "%i  ", tag.m_undefined_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count ) value_name += "...";
               break;
            case GEOTIFF_SSHORT:            // signed short (16-bit) integer
               value_name = "";
               imax = __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STRING_LEN, "%i  ", tag.m_sshort_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count ) value_name += "...";
               break;
            case GEOTIFF_SLONG:             // signed long (32-bit) integer
               value_name = "";
               imax = __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STRING_LEN, "%i  ", tag.m_slong_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count ) value_name += "...";
               break;
            case GEOTIFF_SRATIONAL:         // num/denom pair of signed longs
               value_name = "";
               imax = __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STRING_LEN, "%lf  ", tag.m_double_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count ) value_name += "...";
               break;
            case GEOTIFF_FLOAT:             // 4-byte float value
               value_name = "";
               imax = __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STRING_LEN, "%f  ", tag.m_float_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count ) value_name += "...";
               break;
            case GEOTIFF_DOUBLE:            // 8-byte double value
               value_name = "";
               imax = __min( 100, tag.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STRING_LEN, "%lf  ", tag.m_double_values[i] );
                  value_name += string;
               }
               if( imax < tag.m_count ) value_name += "...";
               break;
            default:
               value_name = "Unknown";
               break;
         }
         break;
   }

   return value_name;
}

// *****************************************************************
// *****************************************************************

std::string CGeoTiffFrameFile::get_geokey_name( unsigned short geokey_id )
{
   // returns the name of the geokey with id geokey_id

   std::string geokey_name;

   switch( geokey_id )
   {
      case GEOTIFF_GT_MODEL_TYPE_GEOKEY:
         geokey_name = "GTModelTypeGeoKey";
         break;
      case GEOTIFF_GT_RASTER_TYPE_GEOKEY:
         geokey_name = "GTRasterTypeGeoKey";
         break;
      case GEOTIFF_GT_CITATION_GEOKEY:
         geokey_name = "GTCitationGeoKey";
         break;
      case GEOTIFF_GEOGRAPHIC_TYPE_GEOKEY:
         geokey_name = "GeographicTypeGeoKey";
         break;
      case GEOTIFF_GEOG_CITATION_GEOKEY:
         geokey_name = "GeogCitationGeoKey";
         break;
      case GEOTIFF_GEOG_GEODETIC_DATUM_GEOKEY:
         geokey_name = "GeogGeodeticDatumGeoKey";
         break;
      case GEOTIFF_GEOG_PRIME_MERIDIAN_GEOKEY:
         geokey_name = "GeogPrimeMeridianGeoKey";
         break;
      case GEOTIFF_GEOG_LINEAR_UNITS_GEOKEY:
         geokey_name = "GeogLinearUnitsGeoKey";
         break;
      case GEOTIFF_GEOG_LINEAR_UNIT_SIZE_GEOKEY:
         geokey_name = "GeogLinearUnitSizeGeoKey";
         break;
      case GEOTIFF_GEOG_ANGULAR_UNITS_GEOKEY:
         geokey_name = "GeogAngularUnitsGeoKey";
         break;
      case GEOTIFF_GEOG_ANGULAR_UNIT_SIZE_GEOKEY:
         geokey_name = "GeogAngularUnitSizeGeoKey";
         break;
      case GEOTIFF_GEOG_ELLIPSOID_GEOKEY:
         geokey_name = "GeogEllipsoidGeoKey";
         break;
      case GEOTIFF_GEOG_SEMI_MAJOR_AXIS_GEOKEY:
         geokey_name = "GeogSemiMajorAxisGeoKey";
         break;
      case GEOTIFF_GEOG_SEMI_MINOR_AXIS_GEOKEY:
         geokey_name = "GeogSemiMinorAxisGeoKey";
         break;
      case GEOTIFF_GEOG_INV_FLATTENING_GEOKEY:
         geokey_name = "GeogInvFlatteningGeoKey";
         break;
      case GEOTIFF_GEOG_AZIMUTH_UNITS_GEOKEY:
         geokey_name = "GeogAzimuthInitsGeoKey";
         break;
      case GEOTIFF_GEOG_PRIME_MERIDIAN_LONG_GEOKEY:
         geokey_name = "GeogPrimeMeridianLongGeoKey";
         break;
      case GEOTIFF_PROJECTED_CS_TYPE_GEOKEY:
         geokey_name = "ProjectedCSTypeGeoKey";
         break;
      case GEOTIFF_PCS_CITATION_GEOKEY:
         geokey_name = "PCSCitationGeoKey";
         break;
      case GEOTIFF_PROJECTION_GEOKEY:
         geokey_name = "ProjectionGeoKey";
         break;
      case GEOTIFF_PROJ_COORD_TRANS_GEOKEY:
         geokey_name = "ProjCoordTransGeoKey";
         break;
      case GEOTIFF_PROJ_LINEAR_UNITS_GEOKEY:
         geokey_name = "ProjLinearUnitsGeoKey";
         break;
      case GEOTIFF_PROJ_LINEAR_UNIT_SIZE_GEOKEY:
         geokey_name = "ProjLinearUnitSizeGeoKey";
         break;
      case GEOTIFF_PROJ_STD_PARALLEL_1_GEOKEY:
         geokey_name = "ProjStdParallel1GeoKey";
         break;
      case GEOTIFF_PROJ_STD_PARALLEL_2_GEOKEY:
         geokey_name = "ProjStdParallel2GeoKey";
         break;
      case GEOTIFF_PROJ_NAT_ORIGIN_LONG_GEOKEY:
         geokey_name = "ProjNatOriginLongGeoKey";
         break;
      case GEOTIFF_PROJ_NAT_ORIGIN_LAT_GEOKEY:
         geokey_name = "ProjNatOriginLatGeoKey";
         break;
      case GEOTIFF_PROJ_FALSE_EASTING_GEOKEY:
         geokey_name = "ProjFalseEastingGeoKey";
         break;
      case GEOTIFF_PROJ_FALSE_NORTHING_GEOKEY:
         geokey_name = "ProjFalseNorthingGeoKey";
         break;
      case GEOTIFF_PROJ_FALSE_ORIGIN_LONG_GEOKEY:
         geokey_name = "ProjFalseOriginLongGeoKey";
         break;
      case GEOTIFF_PROJ_FALSE_ORIGIN_LAT_GEOKEY:
         geokey_name = "ProjFalseOriginLatGeoKey";
         break;
      case GEOTIFF_PROJ_FALSE_ORIGIN_EASTING_GEOKEY:
         geokey_name = "ProjFalseOriginEastingGeoKey";
         break;
      case GEOTIFF_PROJ_FALSE_ORIGIN_NORTHING_GEOKEY:
         geokey_name = "ProjFalseOriginNorthingGeoKey";
         break;
      case GEOTIFF_PROJ_CENTER_LONG_GEOKEY:
         geokey_name = "ProjCenterLongGeoKey";
         break;
      case GEOTIFF_PROJ_CENTER_LAT_GEOKEY:
         geokey_name = "ProjCenterLatGeoKey";
         break;
      case GEOTIFF_PROJ_CENTER_EASTING_GEOKEY:
         geokey_name = "ProjCenterEastingGeoKey";
         break;
      case GEOTIFF_PROJ_CENTER_NORTHING_GEOKEY:
         geokey_name = "ProjCenterNorthingGeoKey";
         break;
      case GEOTIFF_PROJ_SCALE_AT_NAT_ORIGIN_GEOKEY:
         geokey_name = "ProjScaleAtNatOriginGeoKey";
         break;
      case GEOTIFF_PROJ_SCALE_AT_CENTER_GEOKEY:
         geokey_name = "ProjScaleAtCenterGeoKey";
         break;
      case GEOTIFF_PROJ_AZIMUTH_ANGLE_GEOKEY:
         geokey_name = "ProjAzimuthAngleGeoKey";
         break;
      case GEOTIFF_PROJ_STRAIGHT_VERT_POLE_LONG_GEOKEY:
         geokey_name = "ProjStraightVertPoleLongGeoKey";
         break;
      case GEOTIFF_VERTICAL_CS_TYPE_GEOKEY:
         geokey_name = "VertCSTypeGeoKey";
         break;
      case GEOTIFF_VERTICAL_CITATION_GEOKEY:
         geokey_name = "VertCitationGeoKey";
         break;
      case GEOTIFF_VERTICAL_DATUM_GEOKEY:
         geokey_name = "VerticalDatumGeoKey";
         break;
      case GEOTIFF_VERTICAL_UNITS_GEOKEY:
         geokey_name = "VerticalUnitsGeoKey";
         break;
      case GEOTIFF_UNDEFINED:
         geokey_name = "Undefined (0)";
         break;
      case GEOTIFF_USER_DEFINED:
         geokey_name = "User-Defined (32767)";
         break;
      default:
         geokey_name = "Unknown";
         break;
   }

   return geokey_name;
}

// *****************************************************************
// *****************************************************************

std::string CGeoTiffFrameFile::get_geokey_value_name( const CGeoKey &geokey )
{
   // returns the name of the geokey's value if it has a name
   // or it's formatted numeric values otherwise

   int index;
   unsigned short i, imax;
   std::string value_name;
   const int STRING_LEN = 100;
   char string[STRING_LEN];

   value_name = "";

   switch( geokey.m_geokey_id )
   {
      case GEOTIFF_GT_MODEL_TYPE_GEOKEY:        // GTModelTypeGeoKey
 		  if (geokey.m_short_values == NULL)  // sanity check
			  return value_name;
        switch( geokey.m_short_values[0] )
         {
            case GEOTIFF_MODEL_TYPE_PROJECTED:
               value_name = "ModelTypeProjected";
               break;
            case GEOTIFF_MODEL_TYPE_GEOGRAPHIC:
               value_name = "ModelTypeGeographic";
               break;
            case GEOTIFF_MODEL_TYPE_GEOCENTRIC:
               value_name = "ModelTypeGeocentric";
               break;
            case GEOTIFF_UNDEFINED:
               value_name = "Undefined (0)";
               break;
            case GEOTIFF_USER_DEFINED:
               value_name = "User-Defined (32767)";
               break;
            default:
               value_name = "Unknown";
               break;
         }
         break;

      case GEOTIFF_GT_RASTER_TYPE_GEOKEY:          // GTRasterTypeGeoKey
 		  if (geokey.m_short_values == NULL)  // sanity check
			  return value_name;
         switch( geokey.m_short_values[0] )
         {
            case GEOTIFF_RASTER_PIXEL_IS_AREA:
               value_name = "RasterPixelIsArea";
               break;
            case GEOTIFF_RASTER_PIXEL_IS_POINT:
               value_name = "RasterPixelIsPoint";
               break;
            case GEOTIFF_UNDEFINED:
               value_name = "Undefined (0)";
               break;
            case GEOTIFF_USER_DEFINED:
               value_name = "User-Defined (32767)";
               break;
            default:
               value_name = "Unknown";
               break;
         }
         break;

      case GEOTIFF_GEOGRAPHIC_TYPE_GEOKEY:    // GeographicTypeGeoKey
 		  if (geokey.m_short_values == NULL)  // sanity check
			  return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_geographic_type_names; i++ )
         {
            if( geokey.m_short_values[0] ==
                geotiff_geographic_type_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name = "Unknown";
         else
            value_name = geotiff_geographic_type_names[index].name;
         break;

      case GEOTIFF_GEOG_GEODETIC_DATUM_GEOKEY:    // GeogGeodeticDatumGeoKey
 		  if (geokey.m_short_values == NULL)  // sanity check
			  return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_datum_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_datum_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name = "Unknown";
         else
            value_name = geotiff_datum_names[index].name;
         break;

      case GEOTIFF_GEOG_PRIME_MERIDIAN_GEOKEY:   // GeogPrimeMeridianGeoKey
 		  if (geokey.m_short_values == NULL)  // sanity check
			  return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_prime_meridian_names; i++ )
         {
            if( geokey.m_short_values[0] ==
                geotiff_prime_meridian_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name = "Unknown";
         else
            value_name = geotiff_prime_meridian_names[index].name;
         break;

      case GEOTIFF_GEOG_LINEAR_UNITS_GEOKEY:    // GeogLinearUnitsGeoKey
 		  if (geokey.m_short_values == NULL)  // sanity check
			  return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_linear_units_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_linear_units_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name = "Unknown";
         else
            value_name = geotiff_linear_units_names[index].name;
         break;

      case GEOTIFF_GEOG_ANGULAR_UNITS_GEOKEY:   // GeogAngularUnitsGeoKey
 		  if (geokey.m_short_values == NULL)  // sanity check
			  return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_angular_units_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_angular_units_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name = "Unknown";
         else
            value_name = geotiff_angular_units_names[index].name;
         break;

      case GEOTIFF_GEOG_ELLIPSOID_GEOKEY:    // GeogEllipsoidGeoKey
 		  if (geokey.m_short_values == NULL)  // sanity check
			  return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_ellipse_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_ellipse_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name = "Unknown";
         else
            value_name = geotiff_ellipse_names[index].name;
         break;

      case GEOTIFF_GEOG_AZIMUTH_UNITS_GEOKEY:    // GeogAzimuthUnitsGeoKey
 		  if (geokey.m_short_values == NULL)  // sanity check
			  return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_angular_units_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_angular_units_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name = "Unknown";
         else
            value_name = geotiff_angular_units_names[index].name;
         break;

      case GEOTIFF_PROJECTED_CS_TYPE_GEOKEY:      // ProjectedCSTypeGeoKey
 		  if (geokey.m_short_values == NULL)  // sanity check
			  return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_pcs_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_pcs_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name = "Unknown";
         else
            value_name = geotiff_pcs_names[index].name;
         break;

      case GEOTIFF_PROJECTION_GEOKEY:       // ProjectionGeoKey
 		  if (geokey.m_short_values == NULL)  // sanity check
			  return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_proj_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_proj_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name = "Unknown";
         else
            value_name = geotiff_proj_names[index].name;
         break;

      case GEOTIFF_PROJ_COORD_TRANS_GEOKEY:     // ProjCoordTransGeoKey
 		  if (geokey.m_short_values == NULL)  // sanity check
			  return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_ct_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_ct_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name = "Unknown";
         else
            value_name = geotiff_ct_names[index].name;
         break;

      case GEOTIFF_PROJ_LINEAR_UNITS_GEOKEY:    // ProjLinearUnitsGeoKey
 		  if (geokey.m_short_values == NULL)  // sanity check
			  return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_linear_units_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_linear_units_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name = "Unknown";
         else
            value_name = geotiff_linear_units_names[index].name;
         break;

      case GEOTIFF_VERTICAL_CS_TYPE_GEOKEY:     // VerticalCSTypeGeoKey
 		  if (geokey.m_short_values == NULL)  // sanity check
			  return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_vert_cs_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_vert_cs_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name = "Unknown";
         else
            value_name = geotiff_vert_cs_names[index].name;
         break;

      case GEOTIFF_VERTICAL_UNITS_GEOKEY:      // VerticalUnitsGeoKey
 		  if (geokey.m_short_values == NULL)  // sanity check
			  return value_name;
         index = -1;
         for( i = 0; i < geotiff_num_linear_units_names; i++ )
         {
            if( geokey.m_short_values[0] == geotiff_linear_units_names[i].id )
            {
               index = i;
               break;
            }
         }
         if( index == -1 )
            value_name = "Unknown";
         else
            value_name = geotiff_linear_units_names[index].name;
         break;

      default:                              // format data values
         switch( geokey.m_type )
         {
            case GEOTIFF_ASCII:             // null-terminated string
               value_name = "\"";
               value_name += geokey.m_ascii_values;
               value_name += "\"";
               break;
            case GEOTIFF_SHORT:             // unsigned short (16-bit) integer
               value_name = "";
               imax = __min( 100, geokey.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STRING_LEN, "%u  ", geokey.m_short_values[i] );
                  value_name += string;
               }
               if( imax < geokey.m_count ) value_name += "...";
              break;
            case GEOTIFF_DOUBLE:            // 8-byte double value
               value_name = "";
               imax = __min( 100, geokey.m_count );
               for( i = 0; i < imax; i++ )
               {
                  sprintf_s( string, STRING_LEN, "%lf  ", geokey.m_double_values[i] );
                  value_name += string;
               }
               if( imax < geokey.m_count ) value_name += "...";
               break;
            default:
               value_name = "Unknown";
               break;
         }
         break;
   }

   return value_name;
}
// end of get_geokey_value_name

// end of GeoTiff member functions

// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************
// *****************************************************************


gtff::CTiepointTransform::CTiepointTransform( )
{
   m_defined = FALSE;
   m_num_tiepoints = 0;
   m_num_rows = 0;
   m_aprox0 = 1.e-12;
   m_tiepoint_x = NULL;
   m_tiepoint_y = NULL;
   m_tiepoint_latitude = NULL;
   m_tiepoint_longitude = NULL;
   m_x_coef = NULL;
   m_y_coef = NULL;
   m_latitude_coef = NULL;
   m_longitude_coef = NULL;

   clear( );
}

// *****************************************************************
// *****************************************************************

gtff::CTiepointTransform::~CTiepointTransform( )
{
   clear( );
}

// *****************************************************************
// *****************************************************************

void gtff::CTiepointTransform::clear( )
{
   // this function clears the object
   m_defined = FALSE;
   m_num_tiepoints = 0;
   m_num_rows = 0;

   // deallocate memory
   if( m_tiepoint_x != NULL )
   {
      delete [] m_tiepoint_x ;
      m_tiepoint_x = NULL;
   }

   if( m_tiepoint_y != NULL )
   {
      delete [] m_tiepoint_y;
      m_tiepoint_y = NULL;
   }

   if( m_tiepoint_latitude != NULL )
   {
      delete [] m_tiepoint_latitude;
      m_tiepoint_latitude = NULL;
   }

   if( m_tiepoint_longitude != NULL )
   {
      delete [] m_tiepoint_longitude;
      m_tiepoint_longitude = NULL;
   }

   if( m_x_coef != NULL )
   {
      delete [] m_x_coef;
      m_x_coef = NULL;
   }

   if( m_y_coef != NULL )
   {
      delete [] m_y_coef;
      m_y_coef = NULL;
   }

   if( m_latitude_coef != NULL )
   {
      delete [] m_latitude_coef;
      m_latitude_coef = NULL;
   }

   if( m_longitude_coef != NULL )
   {
      delete [] m_longitude_coef;
      m_longitude_coef = NULL;
   }
}

// *****************************************************************
// *****************************************************************

int gtff::CTiepointTransform::define_tiepoints( int num_tiepoints, double *x,
   double *y, double *latitude, double *longitude )
{
   // this function is used to define the tiepoints
   int i, j, index;
   double dx, dy, r_sqrd;
   double *tmp_matrix = NULL;
   double *fwd_matrix = NULL;
   double *inv_matrix = NULL;

   // first clear the object
   clear( );

   // must be at least 3 tiepoints
   if( num_tiepoints < 3 )
      goto FAIL;

   m_num_tiepoints = num_tiepoints;
   m_num_rows = m_num_tiepoints + 3;

   // allocate memory for tiepoints and transformation matrices
   m_tiepoint_x = new double[m_num_tiepoints];
   if( m_tiepoint_x == NULL )
      goto FAIL;
   m_tiepoint_y = new double[m_num_tiepoints];
   if( m_tiepoint_y == NULL )
      goto FAIL;
   m_tiepoint_latitude = new double[m_num_tiepoints];
   if( m_tiepoint_latitude == NULL )
      goto FAIL;
   m_tiepoint_longitude =
      new double[m_num_tiepoints];
   if( m_tiepoint_longitude == NULL )
      goto FAIL;

   m_x_coef = new double[m_num_rows];
   if( m_x_coef == NULL )
      goto FAIL;
   m_y_coef = new double[m_num_rows];
   if( m_y_coef == NULL )
      goto FAIL;
   m_latitude_coef = new double[m_num_rows];
   if( m_latitude_coef == NULL )
      goto FAIL;
   m_longitude_coef = new double[m_num_rows];
   if( m_longitude_coef == NULL )
      goto FAIL;

   // copy tiepoints
   for( i = 0; i < m_num_tiepoints; i++ )
   {
      m_tiepoint_x[i] = x[i];
      m_tiepoint_y[i] = y[i];
      m_tiepoint_latitude[i] = latitude[i];
      m_tiepoint_longitude[i] = longitude[i];
   }

   // allocate temporary memory for calculating matrices
   fwd_matrix =
      new double[m_num_rows*m_num_rows];
   if( fwd_matrix == NULL )
      goto FAIL;
   inv_matrix =
      new double[m_num_rows*m_num_rows];
   if( inv_matrix == NULL )
      goto FAIL;
   tmp_matrix =
      new double[m_num_rows*m_num_rows];
   if( tmp_matrix == NULL )
      goto FAIL;

   // calculate inverse of inverse transformation matrix
   index = 0;
   for( i = 0; i < m_num_tiepoints; i++ )
   {
      for( j = 0; j < m_num_tiepoints; j++ )
      {
         if( j == i )
         {
            tmp_matrix[index] = 0.0;
         }
         else
         {
            dx = m_tiepoint_x[j] - m_tiepoint_x[i];
            dy = m_tiepoint_y[j] - m_tiepoint_y[i];
            r_sqrd = dx*dx + dy*dy;
            // check for coincident points
            if( r_sqrd < m_aprox0 )
               goto FAIL;
            tmp_matrix[index] = r_sqrd * log( r_sqrd );
         }
         index++;
      }

      tmp_matrix[index] = 1.0;
      index++;
      tmp_matrix[index] = m_tiepoint_x[i];
      index++;
      tmp_matrix[index] = m_tiepoint_y[i];
      index++;
   }

   i = m_num_tiepoints;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = 1.0;
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;

   i = m_num_tiepoints + 1;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = m_tiepoint_x[j];
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;

   i = m_num_tiepoints + 2;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = m_tiepoint_y[j];
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;

   // now invert the temporary matrix to obtain the inverse transform matrix
   // which allows calculation of latitude and longitude from x and y
   if( invert_matrix( tmp_matrix, m_num_rows, inv_matrix ) != SUCCESS )
      goto FAIL;

   // now calculate the coefficients for latitude and longitude
   index = 0;
   for( i = 0; i < m_num_rows; i++ )
   {
      m_latitude_coef[i] = 0.0;
      m_longitude_coef[i] = 0.0;

      for( j = 0; j < m_num_tiepoints; j++ )
      {
         m_latitude_coef[i] += inv_matrix[index] * m_tiepoint_latitude[j];
         m_longitude_coef[i] += inv_matrix[index] * m_tiepoint_longitude[j];
         index++;
      }
      index += 3;
   }


   // calculate inverse of forward transformation matrix
   index = 0;
   for( i = 0; i < m_num_tiepoints; i++ )
   {
      for( j = 0; j < m_num_tiepoints; j++ )
      {
         if( j == i )
         {
            tmp_matrix[index] = 0.0;
         }
         else
         {
            dx = m_tiepoint_longitude[j] - m_tiepoint_longitude[i];
            dy = m_tiepoint_latitude[j] - m_tiepoint_latitude[i];
            r_sqrd = dx*dx + dy*dy;
            // check for coincident points
            if( r_sqrd < m_aprox0 )
               goto FAIL;
            tmp_matrix[index] = r_sqrd * log( r_sqrd );
         }
         index++;
      }

      tmp_matrix[index] = 1.0;
      index++;
      tmp_matrix[index] = m_tiepoint_longitude[i];
      index++;
      tmp_matrix[index] = m_tiepoint_latitude[i];
      index++;
   }

   i = m_num_tiepoints;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = 1.0;
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;

   i = m_num_tiepoints + 1;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = m_tiepoint_longitude[j];
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;

   i = m_num_tiepoints + 2;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = m_tiepoint_latitude[j];
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;
   tmp_matrix[index] = 0.0;
   index++;

   // now invert the temporary matrix to obtain the inverse transform matrix
   // which allows calculation of latitude and longitude from x and y
   if( invert_matrix( tmp_matrix, m_num_rows, fwd_matrix ) != SUCCESS )
      goto FAIL;

   // now calculate the coefficients for x and y
   index = 0;
   for( i = 0; i < m_num_rows; i++ )
   {
      m_x_coef[i] = 0.0;
      m_y_coef[i] = 0.0;

      for( j = 0; j < m_num_tiepoints; j++ )
      {
         m_x_coef[i] += fwd_matrix[index] * m_tiepoint_x[j];
         m_y_coef[i] += fwd_matrix[index] * m_tiepoint_y[j];
         index++;
      }
      index += 3;
   }

   // deallocate temporary matrices
   delete [] tmp_matrix;
   delete [] fwd_matrix;
   delete [] inv_matrix;

   m_defined = TRUE;
   return SUCCESS;

FAIL:
   if( tmp_matrix != NULL )
      delete [] tmp_matrix;
   if( fwd_matrix != NULL )
      delete [] fwd_matrix;
   if( inv_matrix != NULL )
      delete [] inv_matrix;
   clear( );
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int gtff::CTiepointTransform::invert_matrix( double *matrix, int n,
                                       double *inverse_matrix )
{
	// this function inverts the input matrix, which is destroyed
	// returns SUCCESS or FAILURE

	int i, j, index;
   int *index_array = NULL;
   double *col;

	if( n <= 0 )
		goto FAIL;

	if( n == 1 )
	{
		if( fabs( matrix[0]) < m_aprox0 )
			goto FAIL;
		inverse_matrix[0] = 1.0 / matrix[0];
		goto SUCCEED;
	}

	// allocate memory for index
	index_array = new int[n];
   if( index_array == NULL )
      goto FAIL;

	// perform LU decomposition, return if error
	if( decompose_lu( matrix, n, index_array ) != SUCCESS )
      goto FAIL;

	// allocate memory for one column
	col = new double[n];
   if( col == NULL )
      goto FAIL;

	// calculate inverse column by column using back substitution
	for( j = 0; j < n; j++ )
	{
		for( i = 0; i < n; i++ )
			col[i] = 0.0;
		col[j] = 1.0;
		back_substitute_lu( matrix, n, index_array, col );
		for( i = 0; i < n; i++ )
		{
			index = i*n + j;
			inverse_matrix[index] = col[i];
		}
	}

SUCCEED:
   if( index_array != NULL )
   {
      delete [] index_array;
      index = NULL;
   }
   if( col != NULL )
   {
      delete [] col;
      col = NULL;
   }
   return SUCCESS;

FAIL:
   if( index_array != NULL )
   {
      delete [] index_array;
      index = NULL;
   }
   if( col != NULL )
   {
      delete [] col;
      col = NULL;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int gtff::CTiepointTransform::decompose_lu( double *a, int n, int *indx )
{
   // performs LU decomposition on matrix in a
   // returns SUCCESS or FAILURE
   int i,imax,j,k;
	int index, index1, index2;
	double d, big,dum,sum,temp;
   double *scratch;

	// allocate scratch space
	scratch = new double[n];
   if( scratch == NULL )
      goto FAIL;

	d=1.0;

	for( i = 0; i < n; i++ )   // find largest absolute value in each row of a
	{
		big = 0.0;
		for( j = 0; j < n; j++ )
		{
			index = n*i+j;
			if( (temp=fabs(a[index])) > big )
				big=temp;
		}
		if ( big == 0.0 )
         goto FAIL;
		scratch[i]=1.0/big; // scratch now contains largest abs value in each row
	}

	for( j = 0; j < n; j++ )
	{
		for( i = 0; i < j; i++ )
		{
			index = n*i+j;
			sum=a[index];
			for ( k = 0; k < i; k++ )
			{
				index1 = n*i+k;
				index2 = n*k+j;
				sum -= a[index1]*a[index2];
			}
			a[index]=sum;
		}

		big=0.0;

		for( i = j; i < n; i++ )
		{
			index = n*i+j;
			sum=a[index];
			for( k = 0; k < j; k++ )
			{
				index1 = n*i+k;
				index2 = n*k+j;
				sum -= a[index1]*a[index2];
			}
			a[index]=sum;
			if ( (dum=scratch[i]*fabs(sum)) >= big)
			{
				big=dum;
				imax=i;
			}
		}

		if( j != imax )
		{
			for( k = 0; k < n; k++)
			{
				index1 = n*imax+k;
				dum=a[index1];
				index2 = n*j+k;
				a[index1]=a[index2];
				a[index2]=dum;
			}
			d = -(d);
			scratch[imax]=scratch[j];
		}

		indx[j]=imax;
		index1 = n*j+j;
		if( a[index1] == 0.0 )
			a[index1] = m_aprox0;
		if (j != n - 1)
		{
			dum=1.0/(a[index1]);
			for( i= j + 1; i < n; i++ )
			{
				index = n*i+j;
				a[index] *= dum;
			}
		}
	}

	if( scratch != NULL )
   {
      delete [] scratch;
      scratch = NULL;
   }
	return SUCCESS;

FAIL:
	if( scratch != NULL )
   {
      delete [] scratch;
      scratch = NULL;
   }
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

void gtff::CTiepointTransform::back_substitute_lu( double *a, int n, int *indx,
                                             double *b )
{
	int i,ii=-1,ip,j;
	int index;
	double sum;

	for( i = 0; i < n; i++ )
	{
		ip=indx[i];
		sum=b[ip];
		b[ip]=b[i];
		if( ii >= 0 )
			for( j = ii; j <= i-1; j++ )
			{
				index = n*i+j;
				sum -= a[index]*b[j];
			}
		else
			if( sum )
				ii=i;
		b[i]=sum;
	}

	for( i = n - 1; i >= 0; i-- )
	{
		sum=b[i];
		for( j = i+1; j < n; j++ )
		{
			index = n*i+j;
			sum -= a[index]*b[j];
		}
		index = n*i+i;
		b[i]=sum/a[index];
	}
}

// *****************************************************************
// *****************************************************************

int gtff::CTiepointTransform::inv_transform( double x, double y,
                                       double &latitude, double &longitude )
{
   // performs inverse transform: returns longitude and latitude corresponding
   // to the arguments x and y
   // returns SUCCESS or FAILURE

   int i;
   double r_sqrd, dx, dy;

   // check for defined transformation
   if( !m_defined )
      goto FAIL;

   longitude = 0.0;
   latitude = 0.0;

   for( i = 0; i < m_num_tiepoints; i++ )
   {
      dx = x - m_tiepoint_x[i];
      dy = y - m_tiepoint_y[i];
      r_sqrd = dx*dx + dy*dy;
      if( r_sqrd < m_aprox0 )
      {
         latitude = m_tiepoint_latitude[i];
         longitude = m_tiepoint_longitude[i];
         goto SUCCEED;
      }
      latitude += r_sqrd * log( r_sqrd ) * m_latitude_coef[i];
      longitude += r_sqrd * log( r_sqrd ) * m_longitude_coef[i];
   }

   latitude += m_latitude_coef[i];
   longitude += m_longitude_coef[i];

   latitude += x * m_latitude_coef[i+1];
   longitude += x * m_longitude_coef[i+1];

   latitude += y * m_latitude_coef[i+2];
   longitude += y * m_longitude_coef[i+2];

SUCCEED:
   return SUCCESS;

FAIL:
   return FAILURE;
}

// *****************************************************************
// *****************************************************************

int gtff::CTiepointTransform::fwd_transform( double latitude, double longitude,
                                       double &x, double &y )
{
   // performs forward transform: returns x and y corresponding
   // to the arguments longitude and latitude
   // returns SUCCESS or FAILURE

   int i;
   double r_sqrd, d_latitude, d_longitude;

   // check for defined transformation
   if( !m_defined )
      goto FAIL;

   x = 0.0;
   y = 0.0;

   for( i = 0; i < m_num_tiepoints; i++ )
   {
      d_latitude = latitude - m_tiepoint_latitude[i];
      d_longitude = longitude - m_tiepoint_longitude[i];
      r_sqrd = d_latitude*d_latitude + d_longitude*d_longitude;
      if( r_sqrd < m_aprox0 )
      {
         x = m_tiepoint_x[i];
         y = m_tiepoint_y[i];
         goto SUCCEED;
      }
      x += r_sqrd * log( r_sqrd ) * m_x_coef[i];
      y += r_sqrd * log( r_sqrd ) * m_y_coef[i];
   }

   x += m_x_coef[i];
   y += m_y_coef[i];

   x += longitude * m_x_coef[i+1];
   y += longitude * m_y_coef[i+1];

   x += latitude * m_x_coef[i+2];
   y += latitude * m_y_coef[i+2];

SUCCEED:
   return SUCCESS;

FAIL:
   return FAILURE;
}

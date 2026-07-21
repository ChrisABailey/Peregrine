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



#ifndef RPF_DEFS_H
#define RPF_DEFS_H

#define RPF_LITTLE_ENDIAN    0xFF
#define RPF_BIG_ENDIAN       0x00

#define RPF_UINT_2_NULL_VALUE 0xFFFF
#define RPF_UINT_4_NULL_VALUE 0xFFFFFFFF

// flags for the new/replacement/update indicator
#define RPF_ORIGINAL    0
#define RPF_REPLACEMENT 1
#define RPF_UPDATE      2

/*
 *  section/component ID codes
 */

typedef UINT2 RPF_component_id;

#define ID_HEADER_COMPONENT                     128
#define ID_LOCATION_COMPONENT                   129
#define ID_COVERAGE_SECTION_SUBHEADER           130
#define ID_COMPRESSION_SECTION_SUBHEADER        131
#define ID_COMPRESSION_LOOKUP_SUBSECTION        132
#define ID_COMPRESSION_PARAMETER_SUBSECTION     133
#define ID_COLOR_GRAYSCALE_SECTION_SUBHEADER    134
#define ID_COLORMAP_SUBSECTION                  135
#define ID_IMAGE_DESCRIPTION_SUBHEADER          136
#define ID_IMAGE_DISPLAY_PARAMETERS_SUBHEADER   137
#define ID_MASK_SUBSECTION                      138
#define ID_COLOR_CONVERTER_SUBSECTION           139
#define ID_SPATIAL_DATA_SUBSECTION              140
#define ID_ATTRIBUTE_SECTION_SUBHEADER          141
#define ID_ATTRIBUTE_SUBSECTION                 142
#define ID_EXPLICIT_AREAL_COVERAGE_TABLE        143
#define ID_RELATED_IMAGES_SECTION_SUBHEADER     144
#define ID_RELATED_IMAGES_SUBSECTION            145
#define ID_REPLACE_UPDATE_SECTION_SUBHEADER     146
#define ID_REPLACE_UPDATE_TABLE                 147
#define ID_BOUNDARY_RECTANGLE_SECTION_SUBHEADER 148
#define ID_BOUNDARY_RECTANGLE_TABLE             149
#define ID_FRAME_FILE_INDEX_SECTION_SUBHEADER   150
#define ID_FRAME_FILE_INDEX_SUBSECTION          151
#define ID_COLOR_TABLE_INDEX_SECTION_SUBHEADER  152
#define ID_COLOR_TABLE_INDEX_RECORD             153

// compression algorithm ids
#define RPF_COMPRESSION_VQ 1

// color table codes
#define RPF_COLOR_TABLE_RGB   ((UINT2) 1)
#define RPF_COLOR_TABLE_RGBM  ((UINT2) 2)
#define RPF_COLOR_TABLE_MONO  ((UINT2) 3)
#define RPF_COLOR_TABLE_CMYK  ((UINT2) 4)

//
//  attribute and parameter ids and related defines
//
// Attribute 1
#define RPF_ATTR_CURRENCY_DATE     1
   #define RPF_PRM_CD_CURRENCY_DATE      1
   #define CURRENCY_DATE_LENGTH 8

// Attribute 2
#define RPF_ATTR_PRODUCTION_DATE   2
   #define RPF_PRM_PD_PRODUCTION_DATE    1
   #define PRODUCTION_DATE_LENGTH 8

// Attribute 3
#define RPF_ATTR_SIGNIFICANT_DATE  3
   #define RPF_PRM_SD_SIGNIFICANT_DATE  1
   #define SIGNIFICANT_DATE_LENGTH 8

// Attribute 4, paramters 1..4
#define RPF_ATTR_MAP_CHART_SOURCE  4
   #define RPF_PRM_MCS_DATA_SERIES_DESIGNATION    1
   #define DATA_SERIES_DESIGNATION_LENGTH 10
   #define RPF_PRM_MCS_MAP_DESIGNATION            2
   #define MAP_DESIGNATION_LENGTH 8
   #define RPF_PRM_MCS_OLD_HORIZONTAL_DATUM_CODE  3
   #define OLD_HORIZONTAL_DATUM_CODE_LENGTH  4
   #define RPF_PRM_MCS_EDITION_IDENTIFIER         4
   #define EDITION_IDENTIFIER_LENGTH 7

// Attribute 5, paramters 1..5
#define RPF_ATTR_PROJECTION_SYSTEM  5
   #define RPF_PRM_PS_PROJECTION_CODE              1
   #define PROJECTION_CODE_LENGTH 2
   #define RPF_PRM_PS_PARAMETER_A                  2
   #define RPF_PRM_PS_PARAMETER_B                  3
   #define RPF_PRM_PS_PARAMETER_C                  4
   #define RPF_PRM_PS_PARAMETER_D                  5


// Attribute 6
#define RPF_ATTR_VERTICAL_DATUM_CODE   6
   #define VERTICAL_DATUM_CODE_LENGTH 4
   #define RPF_PRM_VERTICAL_DATUM_CODE             1

// Attribute 7
#define RPF_ATTR_HORIZONTAL_DATUM_CODE 7
   #define HORIZONTAL_DATUM_CODE_LENGTH 4
   #define RPF_PRM_HORIZONTAL_DATUM_CODE 1

// Attribute 8, parameters 1..2
#define RPF_ATTR_VERT_ABS_ACCURACY     8
   #define RPF_PRM_VERT_ABS_ACCURACY               1
   #define RPF_PRM_VERT_ABS_ACCURACY_UNITS         2

// Attribute 9, parameters 1..2
#define RPF_ATTR_HORIZ_ABS_ACCURACY    9
   #define RPF_PRM_HORIZ_ABS_ACCURACY              1
   #define RPF_PRM_HORIZ_ABS_ACCURACY_UNITS        2

// Attribute 10, parameters 1..2
#define RPF_ATTR_VERT_REL_ACCURACY     10
   #define RPF_PRM_VERT_REL_ACCURACY               1
   #define RPF_PRM_VERT_REL_ACCURACY_UNITS         2

// Attribute 11, parameters 1..2
#define RPF_ATTR_HORIZ_REL_ACCURACY    11
   #define RPF_PRM_HORIZ_REL_ACCURACY              1
   #define RPF_PRM_HORIZ_REL_ACCURACY_UNITS        2

// Attribute 12
#define RPF_ATTR_ELLIPSOID             12
   #define RPF_PRM_ELLISPOID                       1
   #define ELLISPOID_LENGTH 3

// Attribute 13
#define RPF_ATTR_SOUNDING_DATUM        13
   #define RPF_PRM_SOUNDING_DATUM_CODE             1
   #define SOUNDING_DATUM_LENGTH 4

// Attribute 14
#define RPF_ATTR_NAVIGATION_SYSTEM     14
   #define RPF_PRM_NAVIGATION_SYSTEM_CODE          1

// Attribute 15
#define RPF_ATTR_GRID                  15
   #define RPF_PRM_GRID                            1
   #define GRID_LENGTH 2

// Attribute 16, parameters 1..2
#define RPF_ATTR_EAST_ANNUAL_MAG_CHG   16
   #define RPF_PRM_EAST_ANNUAL_MAG_CHG             1
   #define RPF_PRM_UNITS_EAST_MAG_CHANGE           2

// Attribute 17, parameters 1..2
#define RPF_ATTR_WEST_ANNUAL_MAG_CHG   17
   #define RPF_PRM_WEST_ANNUAL_MAG_CHG             1
   #define RPF_PRM_UNITS_WEST_MAG_CHANGE           2

// Attribute 18, paramerters 1..2
#define RPF_ATTR_GRID_N_MAG_NORTH_ANGLE 18
   #define RPF_PRM_GRID_N_MAG_NORTH_ANGLE          1
   #define RPF_PRM_UNITS_N_MAG_ANGLE               2

// Attribute 19, parameters 1..2
#define RPF_ATTR_GRID_CONVERGENCE_ANGLE 19
   #define RPF_PRM_GRID_CONVERGENCE_ANGLE          1
   #define RPF_PRM_UNITS_CONVERGENCE_ANGLE         2

// Attribute 20, parameters 1..4
#define RPF_ATTR_MAXIMUM_ELEVATION      20
   #define RPF_PRM_MAXIMUM_ELEVATION               1
   #define RPF_PRM_UNITS_ELEVATION                 2
   #define RPF_PRM_ELEVATION_LAT                   3
   #define RPF_PRM_ELEVATION_LON                   4

// Attribute 21
#define RPF_ATTR_LEGEND_FILE_NAME       21
   #define RPF_PRM_LEGEND_FILE_NAME                1
   #define LEGEND_FILE_NAME_LENGTH 12

// Attribute 22, parameters 1..2
#define RPF_ATTR_DATA_SOURCE            22
   #define RPF_PRM_DATA_SOURCE                     1
   #define DATA_SOURCE_LENGTH 12
   #define RPF_PRM_GSD                             2

// Attribute 23
#define RPF_ATTR_DATA_LEVEL             23
   #define RPF_PRM_DATA_LEVEL                      1


// Attribute 24, parameters 1..7
// NOTE :: There are two variant length members in this parameter!!!
#define RPF_ATTR_CHART_UPDATE_INFO      24
   #define RPF_PRM_NUM_UPDATES                     1
   #define RPF_PRM_UPDATE_NUMBER                   2
   #define RPF_PRM_UPDATE_DATE                     3
   #define UPDATE_DATE_LENGTH 8
   #define RPF_PRM_NUM_SUBFRAME_IMPACTED           4
   #define RPF_PRM_SUBFRAME_ARRAY                  5
   #define RPF_PRM_CHANGE_DESCR_LEN                6
   #define RPF_PRM_CHANGE_DESCR                    7

// Attribute 25, parameters 1..2
#define RPF_ATTR_CONTOUR_INTERVAL       25
   #define RPF_PRM_CONTOUR_INTERVAL                1
   #define RPF_PRM_CONTOUR_INTERVAL_UNITS          2

#endif

// Copyright (c) 1994-2010 Georgia Tech Research Corporation, Atlanta, GA
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



#pragma once

/*------------------------------------------------------------------
-                            Includes
-------------------------------------------------------------------*/

#include "common.h"  // for degrees_t, radians_t, minutes_t

/*------------------------------------------------------------------
-                           Definitions
-------------------------------------------------------------------*/

// These constants were taken out of NIMA TR8350.2, Third Edition, 4 July 1997.
// NIMA TR8350.2 defined the WGS-84 ellipsoid model.
#define WGS84_a_METERS  6378137.0      // Radius at the equator (semi-major)
#define WGS84_1_f       298.257223563  // reciprocal of flattening 1/f
#define WGS84_b_METERS  6356752.3142   // semi-minor axis radius
#define WGS84_R1_METERS 6371008.7714   // mean radius of semi-axes
#define WGS84_R2_METERS 6371007.1809   // radius of sphere of equal area
#define WGS84_R3_METERS 6371000.7900   // radius of sphere of equal volume
#define WGS84_e         8.18191908426e-2 // ellipsoidal eccentricity

/* According to the National Institute of Standards and Technology (NIST) a 
   nautical mile is defined as 1852 m (6076.115 ft).  NM_TO_METERS and 
   METERS_TO_NM have been updated to reflect this value.

   For more information see http://physics.nist.gov/cuu/Units/outside.html
   It has been confirmed that NIMA uses this value.
*/

#define DEG_TO_MIN(degrees)       (((double)(degrees)) * 60.0)
#define DEG_TO_SEC(degrees)       (((double)(degrees)) * 3600.0)
#define DEG_TO_RAD(degrees)       (((double)(degrees)) * 1.7453292519943295e-2)
#define MIN_TO_DEG(minutes)       (((double)(minutes)) * 1.6666666666666667e-2)
#define MIN_TO_SEC(minutes)       (((double)(minutes)) * 60.0)
#define MIN_TO_RAD(minutes)       (((double)(minutes)) * 2.9088820866572158e-4)
#define RAD_TO_DEG(radians)       (((double)(radians)) * 57.295779513082322)
#define RAD_TO_MIN(radians)       (((double)(radians)) * 3437.7467707849395)
#define SEC_TO_DEG(seconds)       (((double)(seconds)) * 2.7777777777777778e-4)
#define SEC_TO_MIN(seconds)       (((double)(seconds)) * 1.6666666666666667e-2)
#define NM_TO_METERS(naut_miles)  (((double)(naut_miles)) * 1852)
#define METERS_TO_NM(meters)      (((double)(meters)) *  5.399568034557e-4)
#define FEET_TO_METERS(feet)      (((double)(feet)) * 0.3048)
#define METERS_TO_FEET(meters)    (((double)(meters)) * 3.28083989501)
#define FEET_PER_S_TO_KNOTS(m_per_s) (((double)(m_per_s)) * 0.5924838)
#define KNOTS_TO_FEET_PER_S(knots)  (((double)(knots)) * 1.6878207144177139214)
#define KNOTS_TO_KM_PER_H(knots) (((double)(knots)) * 1.8520119135162691316)
                                                        
#define WORLD_MIN 21600.0
#define HALF_WORLD_MIN 10800.0
#define MAX_LAT_MIN 5400.0
#define MAX_LON_MIN 10800.0
#define MIN_LAT_MIN -5400.0
#define MIN_LON_MIN -10800.0

#define WORLD_DEG 360.0
#define HALF_WORLD_DEG 180.0 
#define MAX_LAT_DEG 90.0 
#define MAX_LON_DEG 180.0 
#define MIN_LAT_DEG -90.0 
#define MIN_LON_DEG -180.0 

#define WORLD_RAD TWO_PI 
#define HALF_WORLD_RAD PI 
#define MAX_LAT_RAD HALF_PI 
#define MAX_LON_RAD PI 
#define MIN_LAT_RAD -HALF_PI 
#define MIN_LON_RAD -PI 

#define GEO_NORTH_OF ((int)0x01)
#define GEO_SOUTH_OF ((int)0x02)
#define GEO_EAST_OF  ((int)0x04)
#define GEO_WEST_OF  ((int)0x08)

/*------------------------------------------------------------------
-                            Typedefs 
-------------------------------------------------------------------*/

/* geo coordinate in degrees */
typedef struct {
	degrees_t lat;
	degrees_t lon;
} d_geo_t;

/* geo coordinate in radians */
typedef struct {
	radians_t lat;
	radians_t lon;
} r_geo_t;

/* geo coordinate in minutes */
typedef struct {
	minutes_t lat;
	minutes_t lon;
} m_geo_t;

// geo coordinate rect in degrees
struct d_geo_rect_t
{
   d_geo_t ll; // lower-left corner
   d_geo_t ur; // upper-right corner
};

// geo coordinate rect in radians
struct r_geo_rect_t
{
   r_geo_t ll; // lower-left corner
   r_geo_t ur; // upper-right corner
};

// geo coordinate rect in minutes
struct m_geo_rect_t
{
   m_geo_t ll; // lower-left corner
   m_geo_t ur; // upper-right corner
};

// End of geo_tool.h

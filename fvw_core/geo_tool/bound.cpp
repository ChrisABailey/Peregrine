// Copyright (c) 1994-2009,2013 Georgia Tech Research Corporation, Atlanta, GA
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



/*------------------------------------------------------------------
-  FILE NAME:           bound.c
-  LIBRARY NAME:        geo_tool
-
-  DESCRIPTION:
-
-      These functions return TRUE if the point p is in the geographic
-  bounds defined by ur and ll, and they return FALSE if it is completely
-  outside the bounds. If the point lies on the bounding rectangle then
-  it is considered inside the bounds and the function returns TRUE. 
-
-  PUBLIC FUNCTIONS:
-
-      GEO_in_bounds_degrees
-      GEO_in_bounds
-      GEO_lon_in_range
-
-  PRIVATE FUNCTIONS: NONE
-
-  STATIC FUNCTIONS: NONE
-
-  PUBLIC VARIABLES: NONE
-
-  PRIVATE VARIABLES: NONE
-
-  REVISION HISTORY:
-       $Log: bound.cpp $
//Revision 1.1  1995/04/23  16:59:16  vinny
//Initial revision
//
 * Revision 1.1  1994/10/26  09:15:22  gue
 * Initial revision
 * 
 * Revision 1.1  1994/02/28  07:21:19  gue
 * Initial revision
 * 
-------------------------------------------------------------------*/

/*------------------------------------------------------------------
-                            Includes
-------------------------------------------------------------------*/

#include "stdafx.h"
#include "geo_tool.h"
#include "math.h"

/*------------------------------------------------------------------
-  FUNCTION NAME:    GEO_in_bounds_degrees
-  PROGRAMMER:       Vincent Sollicito
-  DATE:             January 1994
-
-  PURPOSE:
-
-      Test if a geographic point (given in degrees) lies on or within
-  a bounding rectangle.
-
-  PARAMETERS:
-
-      ll:            lower left corner of the bounding rectangle (in degrees)
-
-      ur:            upper right corner of the bounding rectangle (in degrees)
-
-      p:             the point to test for (in degrees)
-
-  RETURN VALUES:
-
-      TRUE
-      FALSE
-
-  PRECONDITIONS: NONE
-
-  EXTERNALS MODIFIED: NONE
-
-  REQUIRED INCLUDES:
-
-      geo_tool.h
-      common.h
-
-  DESCRIPTION: see Purpose
-------------------------------------------------------------------*/

boolean_t GEO_in_bounds_degrees(const d_geo_t& ll, const d_geo_t& ur, const d_geo_t& p)
{
   return GEO_in_bounds(ll.lat, ll.lon, ur.lat, ur.lon, p.lat, p.lon);
}

/*------------------------------------------------------------------
-  FUNCTION NAME:    GEO_in_bounds
-  PROGRAMMER:       Vincent Sollicito
-  DATE:             January 1994
-
-  PURPOSE:
-
-      Test if a geographic point lies on or within a bounding rectangle.
-  All points are in geo coordinates in the same unit.
-
-  PARAMETERS:
-
-      ll_lat:     latitude of the lower left corner of the bounding rectangle
-
-      ll_lon:     longitude of the lower left corner of the bounding rectangle
-
-      ur_lat:     latitude of the upper right corner of the bounding rectangle
-
-      ur_lon:     longitude of the upper right corner of the bounding rectangle
-
-      p_lat:      latitude of the point to test for
-
-      p_lon:      longitude of the point to test for
-
-  RETURN VALUES:
-
-      TRUE
-      FALSE
-
-  PRECONDITIONS: NONE
-
-  EXTERNALS MODIFIED: NONE
-
-  REQUIRED INCLUDES:
-
-      geo_tool.h
-      common.h
-
-  DESCRIPTION: see Purpose
-------------------------------------------------------------------*/

boolean_t GEO_in_bounds(double ll_lat, double ll_lon,
   double ur_lat, double ur_lon,
   double p_lat,  double p_lon)
{
   /* north/south reject */
   if (p_lat < ll_lat || p_lat > ur_lat)
      return FALSE;

   /* east/west reject */
   return GEO_lon_in_range(ll_lon, ur_lon, p_lon);
}

/*------------------------------------------------------------------
-  FUNCTION NAME:      GEO_lon_in_range
-  PROGRAMMER:         Vincent Sollicito
-  DATE:               January 1994
-
-  PURPOSE:
-
-  PARAMETERS:
-
-      left_lon:
-
-      right_lon:
-
-      point_lon:
-
-  RETURN VALUES:
-
-      TRUE
-      FALSE
-
-  PRECONDITIONS: NONE
-
-  EXTERNALS MODIFIED: NONE
-
-  REQUIRED INCLUDES:
-
-      geo_tool.h
-      common.h
-
-  DESCRIPTION: 
-
-  See also Purpose.
-  All points are in geo coordinates in the same unit.
-------------------------------------------------------------------*/

boolean_t GEO_lon_in_range(double western_lon, double eastern_lon, double point_lon)
{
   if (western_lon <= eastern_lon)
   {
      if (point_lon < western_lon || eastern_lon < point_lon)
         return FALSE;
   }
   else // if (left_lon > right_lon)   dvl :: redundant check
   {
      if (point_lon < western_lon && point_lon > eastern_lon)
         return FALSE;
   }

   return TRUE;    /* point in bounds */
} 

/*------------------------------------------------------------------
-  FUNCTION NAME:      GEO_lon_in_deg_range
-  PROGRAMMER:         Jay Smith -- adapted from function above
-  DATE:               March 1997
-
-  PURPOSE:            Performs same function as above except that
-                      it only checks whole degrees.
-
-  PARAMETERS:
-
-      left_lon:
-
-      right_lon:
-
-      point_lon:
-
-  RETURN VALUES:
-
-      TRUE
-      FALSE
-
-  PRECONDITIONS: NONE
-
-  EXTERNALS MODIFIED: NONE
-
-  REQUIRED INCLUDES:
-
-      geo_tool.h
-      common.h
-
-  DESCRIPTION: 
-
-  See also Purpose.
-  All points are in geo coordinates in the same unit.
-------------------------------------------------------------------*/

boolean_t GEO_lon_in_deg_range(int left_lon, int right_lon, 
                               int point_lon)
{
   if (left_lon <= right_lon)
   {
      if (point_lon < left_lon || right_lon < point_lon)
         return FALSE;
   }
   else // if (left_lon > right_lon)      dvl :: redundant check!
   {
      if (point_lon < left_lon && point_lon > right_lon)
         return FALSE;
   }

   return TRUE;    /* point in bounds */
} 

// Given a valid geo-bounds this function computes the width in degrees
// longitude and the height in degrees latitude.
int GEO_calc_dimensions_degrees(const d_geo_t& ll, const d_geo_t& ur, 
                               degrees_t &geo_width, degrees_t &geo_height)
{
   if (!GEO_valid_degrees(ll.lat, ll.lon))
   {
      ERR_report("Lower left corner invalid.");
      return FAILURE;
   }

   if (!GEO_valid_degrees(ur.lat, ur.lon))
   {
      ERR_report("Upper right corner invalid.");
      return FAILURE;
   }

   if (ll.lat > ur.lat)
   {
      ERR_report("Not a valid geo-bounds.");
      return FAILURE;
   }

   geo_height = ur.lat - ll.lat;
   geo_width = ur.lon - ll.lon;
   if (geo_width <= 0.0)
      geo_width += 360.0;
   
   return SUCCESS;
}

// Given a center, a width in degrees longitude, and a height in degrees 
// latitude, this function computes the geo-bounds.  If the geo-bounds pass
// the North Pole they will be shifted to the south so that ur.lat is 90.  If
// the geo-bounds pass the South Pole they will be shifted to the north so
// that ll.lat is -90.
int GEO_center_to_bounds(const d_geo_t& center, degrees_t geo_width, 
   degrees_t geo_height, d_geo_t &ll, d_geo_t &ur)
{
   if (!GEO_valid_degrees(center.lat, center.lon))
   {
      ERR_report("Invalid center.");
      return FAILURE;
   }

   if (geo_width <= 0.0 || geo_width > 360.0)
   {
      ERR_report("geo_width is out of range.");
      return FAILURE;
   }

   if (geo_height <= 0.0 || geo_height > 180.0)
   {
      ERR_report("geo_height is out of range.");
      return FAILURE;
   }

   // avoid the poles
   ur.lat = __min( +90.0, center.lat + ( 0.5 * geo_height ) );
   ll.lat = __max( -90.0, ur.lat - geo_height );

   // set longitudes
   ur.lon = center.lon + ( 0.5 * geo_width );
   while ( ur.lon > 180.0 )
      ur.lon -= 360.0;

   ll.lon = ur.lon - geo_width;
   while ( ll.lon < -180.0 )
      ll.lon += 360.0;

   return SUCCESS;
}

/* These functions test to see if the given line passes through the given
   geographic window. If any part or all of the line pass through the
   window then the fuction returns TRUE, otherwise it returns FALSE. It
   is REQUIRED that the given line spans less than 180.0 degrees of
   longitude, as the points will be interpreted to define the shortest
   line between the two points. */

/*
boolean_t GEO_line_test_radians(radians_t window_ll_lat, 
                                radians_t window_ll_lon,
                                radians_t window_ur_lat, 
                                radians_t window_ur_lon,
                                radians_t p1_lat, radians_t p1_lon,
                                radians_t p2_lat, radians_t p2_lon);
*/

boolean_t GEO_line_test_degrees(degrees_t window_ll_lat, 
                                degrees_t window_ll_lon,
                                degrees_t window_ur_lat, 
                                degrees_t window_ur_lon,
                                degrees_t p1_lat, degrees_t p1_lon,
                                degrees_t p2_lat, degrees_t p2_lon)
{
   CGeorect georect;
   CGeoline geoline;

   georect.set( window_ll_lat, window_ll_lon, window_ur_lat, window_ur_lon );
   geoline.set( p1_lat, p1_lon, p2_lat, p2_lon );

   return georect.intersect( geoline );
}

/*
boolean_t GEO_line_test_minutes(minutes_t window_ll_lat,
                                minutes_t window_ll_lon,
                                minutes_t window_ur_lat, 
                                minutes_t window_ur_lon,
                                minutes_t p1_lat, minutes_t p1_lon,
                                minutes_t p2_lat, minutes_t p2_lon);
*/

// A proper geoline has a left endpoint and a right endpoint and does not
// cross the international date line. So, the left endpoint longitude is
// always <= the right point longitude. A geoline can consist of one or two
// proper geolines.

// default constructor
CProperGeoline::CProperGeoline( )
{
   m_left_lat = 0.0;
   m_left_lon = 0.0;
   m_right_lat = 0.0;
   m_right_lon = 0.0;
}
                                
// constructor that takes the left and right endpoint lat and lon
CProperGeoline::CProperGeoline( degrees_t left_lat, degrees_t left_lon, degrees_t right_lat, degrees_t right_lon )
{
   set( left_lat, left_lon, right_lat, right_lon );
}
                                
// destructor
CProperGeoline::~CProperGeoline( )
{
}

// sets the left and right endpoint lat and lon
void CProperGeoline::set( degrees_t left_lat, degrees_t left_lon, degrees_t right_lat, degrees_t right_lon )
{
   // limit to +/-90 lat and +/-180 lon
   if( left_lat < -90.0 )
      left_lat = -90.0;
   if( left_lat > 90.0 )
      left_lat = 90.0;
   if( left_lon < -180.0 )
      left_lon = -180.0;
   if( left_lon > 180.0 )
      left_lon = 180.0;
   if( right_lat < -90.0 )
      right_lat = -90.0;
   if( right_lat > 90.0 )
      right_lat = 90.0;
   if( right_lon < -180.0 )
      right_lon = -180.0;
   if( right_lon > 180.0 )
      right_lon = 180.0;
   
   if( left_lon <= right_lon )
   {
      m_left_lat = left_lat;
      m_left_lon = left_lon;
      m_right_lat = right_lat;
      m_right_lon = right_lon;
   }
   else
   {
      m_left_lat = right_lat;
      m_left_lon = right_lon;
      m_right_lat = left_lat;
      m_right_lon = left_lon;
   }
}
                                
// returns the left and right endpoint lat and lon
void CProperGeoline::get( degrees_t &left_lat, degrees_t &left_lon, degrees_t &right_lat, degrees_t &right_lon ) const
{
   left_lat = m_left_lat;
   left_lon = m_left_lon;
   right_lat = m_right_lat;
   right_lon = m_right_lon;
}

// returns TRUE if the proper geoline is intersected by the given latitude and
// returns the longitude at intersection.
BOOL CProperGeoline::longitude_at_latitude( degrees_t lat, degrees_t &lon ) const
{
   double delta_lat, delta_lon, slope;

   // check for valid lat
   if( lat < -90.0 || lat > 90.0 )
      return FALSE;
   
   // first see if the lat is above or below both endpoints (no intersection)
   if( lat < m_left_lat && lat < m_right_lat )
      return FALSE;
   if( lat > m_left_lat && lat > m_right_lat )
      return FALSE;

   delta_lat = m_right_lat - m_left_lat;
   delta_lon = m_right_lon - m_left_lon;
   
   // test for horizontal line
   if( fabs(delta_lat) < 1.e-10 )
   {
      lon = m_left_lon;
      return TRUE;
   }

   slope = delta_lon / delta_lat;

   lon = m_left_lon + ( lat - m_left_lat ) * slope;
   return TRUE;
}

BOOL CProperGeoline::latitude_at_longitude( degrees_t lon, degrees_t &lat ) const
{
   double delta_lat, delta_lon, slope;

   // check for valid lon
   if( lon < -180.0 || lon > 180.0 )
      return FALSE;
   
   // first see if the lon is right or left of both endpoints (no intersection)
   if( lon < m_left_lon || lon > m_right_lon )
      return FALSE;

   delta_lat = m_right_lat - m_left_lat;
   delta_lon = m_right_lon - m_left_lon;
   
   // test for vertical line
   if( delta_lon < 1.e-10 )
   {
      lat = m_left_lat;
      return TRUE;
   }

   slope = delta_lat / delta_lon;

   lat = m_left_lat + ( lon - m_left_lon ) * slope;
   return TRUE;
}


// A geoline has a left endpoint and a right endpoint and can
// cross the international date line. If so, the left endpoint longitude is
// greater than the right point longitude. Internally, the geoline is stored 
// as one or two propper geolines.

// default constructor
CGeoline::CGeoline( )
{
   m_left_lat = 0.0;
   m_left_lon = 0.0;
   m_right_lat = 0.0;
   m_right_lon = 0.0;

   m_num_proper_geolines = 1;
   m_proper_geolines[0].set( 0.0, 0.0, 0.0, 0.0 );
   m_proper_geolines[1].set( 0.0, 0.0, 0.0, 0.0 );
}
                                
// constructor that takes the left and right endpoint lat and lon
CGeoline::CGeoline( degrees_t left_lat, degrees_t left_lon, degrees_t right_lat, degrees_t right_lon )
{
   set( left_lat, left_lon, right_lat, right_lon );
}
                                
// destructor
CGeoline::~CGeoline( )
{
}

// sets the left and right endpoint lat and lon
void CGeoline::set( degrees_t left_lat, degrees_t left_lon, degrees_t right_lat, degrees_t right_lon )
{
   degrees_t temp_lat, temp_lon, intersection_lat;
   double delta_lat, delta_lon, slope;

   // limit to +/-90 lat and +/-180 lon
   if( left_lat < -90.0 )
      left_lat = -90.0;
   if( left_lat > 90.0 )
      left_lat = 90.0;
   if( left_lon < -180.0 )
      left_lon = -180.0;
   if( left_lon > 180.0 )
      left_lon = 180.0;
   if( right_lat < -90.0 )
      right_lat = -90.0;
   if( right_lat > 90.0 )
      right_lat = 90.0;
   if( right_lon < -180.0 )
      right_lon = -180.0;
   if( right_lon > 180.0 )
      right_lon = 180.0;

   // arrange endpoints for shortest distance
   if( left_lon <= right_lon )
   {
      if( right_lon - left_lon > 180.0 )
      {
         temp_lat = left_lat;
         temp_lon = left_lon;
         left_lat = right_lat;
         left_lon = right_lon;
         right_lat = temp_lat;
         right_lon = temp_lon;
      }
   }
   else
   {
      if( left_lon - right_lon <= 180.0 )
      {
         temp_lat = left_lat;
         temp_lon = left_lon;
         left_lat = right_lat;
         left_lon = right_lon;
         right_lat = temp_lat;
         right_lon = temp_lon;
      }
   }
   
   m_left_lat = left_lat;
   m_left_lon = left_lon;
   m_right_lat = right_lat;
   m_right_lon = right_lon;

   if( m_left_lon <= m_right_lon )
   {
      m_num_proper_geolines = 1;
      m_proper_geolines[0].set( m_left_lat, m_left_lon, m_right_lat, m_right_lon );
      m_proper_geolines[1].set( 0.0, 0.0, 0.0, 0.0 );
   }
   else
   {
      delta_lat = m_right_lat - m_left_lat;
      delta_lon = m_right_lon + 360.0 - m_left_lon;
      if( delta_lon < 1.e-7 )
      {
         intersection_lat = (m_left_lat + m_right_lat) / 2.0;
      }
      else
      {
         slope = delta_lat / delta_lon;
         intersection_lat = m_left_lat + slope * (180.0-m_left_lon);
      }
      m_num_proper_geolines = 2;
      m_proper_geolines[0].set( m_left_lat, m_left_lon, intersection_lat, 180.0 );
      m_proper_geolines[1].set( intersection_lat, -180.0, m_right_lat, m_right_lon );
   }
}
                                
// returns the left and right endpoint lat and lon
void CGeoline::get( degrees_t &left_lat, degrees_t &left_lon, degrees_t &right_lat, degrees_t &right_lon ) const
{
   left_lat = m_left_lat;
   left_lon = m_left_lon;
   right_lat = m_right_lat;
   right_lon = m_right_lon;
}

// returns TRUE if the proper geoline is intersected by the given latitude and
// returns the longitude at intersection.
BOOL CGeoline::longitude_at_latitude( degrees_t lat, degrees_t &lon ) const
{
   int i;
   
   // check for valid lat
   if( lat < -90.0 || lat > 90.0 )
      return FALSE;
   
   // test the proper geolines
   for( i = 0; i < m_num_proper_geolines; i++ )
   {
      if( m_proper_geolines[i].longitude_at_latitude( lat, lon ) )
         return TRUE;
   }

   // if none of the proper geolines intersect, return FALSE
   return FALSE;
}

BOOL CGeoline::latitude_at_longitude( degrees_t lon, degrees_t &lat ) const
{
   int i;
   
   // check for valid on
   if( lon < -180.0 || lon > 180.0 )
      return FALSE;
   
   // test the proper geolines
   for( i = 0; i < m_num_proper_geolines; i++ )
   {
      if( m_proper_geolines[i].latitude_at_longitude( lon, lat ) )
         return TRUE;
   }

   // if none of the proper geolines intersect, return FALSE
   return FALSE;
}

// returns the proper geoline with the specified index if it exists
BOOL CGeoline::get_proper_geoline( int index, CProperGeoline& proper_geoline ) const
{
   if( index < 0 || index >= m_num_proper_geolines )
      return FALSE;
   
   proper_geoline = m_proper_geolines[index];
   return TRUE;
}
   
   
// A proper georect is a rectangular region which does not cross the international
// date line. It is defined by its max/min lat and lon. A general georect, which
// can cross the international date line, consists of one or two proper georects.

// default constructor
CProperGeorect::CProperGeorect( )
{
   m_min_lat = 0.0;
   m_min_lon = 0.0;
   m_max_lat = 0.0;
   m_max_lon = 0.0;
}

// constructor which takes the max and min lat and lon.
CProperGeorect::CProperGeorect( degrees_t min_lat, degrees_t min_lon, degrees_t max_lat, degrees_t max_lon )
{
   set( min_lat, min_lon, max_lat, max_lon );
}

// destructor
CProperGeorect::~CProperGeorect( )
{
}

// sets the max and min lat and lon
void CProperGeorect::set( degrees_t min_lat, degrees_t min_lon, degrees_t max_lat, degrees_t max_lon )
{
   // limit to +/-90 lat and +/-180 lon
   if( min_lat < -90.0 )
      min_lat = -90.0;
   if( min_lat > 90.0 )
      min_lat = 90.0;
   if( min_lon < -180.0 )
      min_lon = -180.0;
   if( min_lon > 180.0 )
      min_lon = 180.0;
   if( max_lat < -90.0 )
      max_lat = -90.0;
   if( max_lat > 90.0 )
      max_lat = 90.0;
   if( max_lon < -180.0 )
      max_lon = -180.0;
   if( max_lon > 180.0 )
      max_lon = 180.0;
   
   if( min_lat <= max_lat )
   {
      m_min_lat = min_lat;
      m_max_lat = max_lat;
   }
   else
   {
      m_min_lat = max_lat;
      m_max_lat = min_lat;
   }

   if( min_lon <= max_lon )
   {
      m_min_lon = min_lon;
      m_max_lon = max_lon;
   }
   else
   {
      m_min_lon = max_lon;
      m_max_lon = min_lon;
   }
}

// returns the max and min lat and lon
void CProperGeorect::get( degrees_t &min_lat, degrees_t &min_lon, degrees_t &max_lat, degrees_t &max_lon ) const
{
   min_lat = m_min_lat;
   min_lon = m_min_lon;
   max_lat = m_max_lat;
   max_lon = m_max_lon;
}

// returns TRUE if the point is within the proper georect.
BOOL CProperGeorect::intersect( degrees_t lat, degrees_t lon ) const
{
   if( lat >= m_min_lat && lat <= m_max_lat && lon >= m_min_lon && lon <= m_max_lon )
      return TRUE;
   else
      return FALSE;
}

// returns TRUE if the proper geoline intersects the proper georect.
BOOL CProperGeorect::intersect( const CProperGeoline& proper_geoline ) const
{
   degrees_t left_lat, left_lon, right_lat, right_lon;
   degrees_t intersection_lat, intersection_lon;

   // get the proper geoline endpoints
   proper_geoline.get( left_lat, left_lon, right_lat, right_lon );

   // test for either endpoint within georect
   if( intersect( left_lat, left_lon ) )
      return TRUE;
   if( intersect( right_lat, right_lon ) )
      return TRUE;

   // test for intersection of line with georect edges
   if( proper_geoline.latitude_at_longitude( m_min_lon, intersection_lat ) )
   {
      if( intersection_lat >= m_min_lat && intersection_lat <= m_max_lat )
         return TRUE;
   }
   if( proper_geoline.latitude_at_longitude( m_max_lon, intersection_lat ) )
   {
      if( intersection_lat >= m_min_lat && intersection_lat <= m_max_lat )
         return TRUE;
   }
   if( proper_geoline.longitude_at_latitude( m_min_lat, intersection_lon ) )
   {
      if( intersection_lon >= m_min_lon && intersection_lon <= m_max_lon )
         return TRUE;
   }
   if( proper_geoline.longitude_at_latitude( m_max_lat, intersection_lon ) )
   {
      if( intersection_lon >= m_min_lon && intersection_lon <= m_max_lon )
         return TRUE;
   }
   
   return FALSE;
}

// returns TRUE if the two proper georects overlap
BOOL CProperGeorect::intersect( const CProperGeorect& proper_georect ) const
{
   degrees_t min_lat, min_lon, max_lat, max_lon;      

   // get the other proper georect's max/min lat and lon
   proper_georect.get( min_lat, min_lon, max_lat, max_lon );

   // check for latitude overlap
   if( min_lat > m_max_lat || max_lat < m_min_lat )
      return FALSE;

   // check for longitude overlap
   if( min_lon > m_max_lon || max_lon < m_min_lon )
      return FALSE;

   return TRUE;
}

// A georect is a rectangular region which can cross the international
// date line. It is defined by its lower left and upper right lat and lon.
// Internally, the georect id stored as one or two proper georects.

// default constructor
CGeorect::CGeorect( )
{
   m_ll_lat = 0.0;
   m_ll_lon = 0.0;
   m_ur_lat = 0.0;
   m_ur_lon = 0.0;

   m_num_proper_georects = 1;
   m_proper_georects[0].set( 0.0, 0.0, 0.0, 0.0 );
   m_proper_georects[1].set( 0.0, 0.0, 0.0, 0.0 );
}

// constructor which takes the lower left and upper right lat and lon.
CGeorect::CGeorect( degrees_t ll_lat, degrees_t ll_lon, degrees_t ur_lat, degrees_t ur_lon )
{
   set( ll_lat, ll_lon, ur_lat, ur_lon );
}

// destructor
CGeorect::~CGeorect( )
{
}

// sets the lower left and upper right lat and lon
void CGeorect::set( degrees_t ll_lat, degrees_t ll_lon, degrees_t ur_lat, degrees_t ur_lon )
{
   // limit to +/-90 lat and +/-180 lon
   if( ll_lat < -90.0 )
      ll_lat = -90.0;
   if( ll_lat > 90.0 )
      ll_lat = 90.0;
   if( ll_lon < -180.0 )
      ll_lon = -180.0;
   if( ll_lon > 180.0 )
      ll_lon = 180.0;
   if( ur_lat < -90.0 )
      ur_lat = -90.0;
   if( ur_lat > 90.0 )
      ur_lat = 90.0;
   if( ur_lon < -180.0 )
      ur_lon = -180.0;
   if( ur_lon > 180.0 )
      ur_lon = 180.0;
   
   m_ll_lat = ll_lat;
   m_ur_lat = ur_lat;
   m_ll_lon = ll_lon;
   m_ur_lon = ur_lon;

   if( m_ll_lon <= m_ur_lon )
   {
      m_num_proper_georects = 1;
      m_proper_georects[0].set( m_ll_lat, m_ll_lon, m_ur_lat, m_ur_lon );
      m_proper_georects[1].set( 0.0, 0.0, 0.0, 0.0 );
   }
   else
   {
      m_num_proper_georects = 2;
      m_proper_georects[0].set( m_ll_lat, m_ll_lon, m_ur_lat, 180.0 );
      m_proper_georects[1].set( m_ll_lat, -180.0, m_ur_lat, m_ur_lon );
   }
}

// returns the ur and ll lat and lon
void CGeorect::get( degrees_t &ll_lat, degrees_t &ll_lon, degrees_t &ur_lat, degrees_t &ur_lon ) const
{
   ll_lat = m_ll_lat;
   ll_lon = m_ll_lon;
   ur_lat = m_ur_lat;
   ur_lon = m_ur_lon;
}

// returns TRUE if the point is within the proper georect.
BOOL CGeorect::intersect( degrees_t lat, degrees_t lon ) const
{
   int i;

   // test the proper georects
   for( i = 0; i < m_num_proper_georects; i++ )
   {
      if( m_proper_georects[i].intersect( lat, lon ) )
         return TRUE;
   }
   
   return FALSE;
}

// returns TRUE if the proper geoline intersects the proper georect.
BOOL CGeorect::intersect( const CGeoline& geoline ) const
{
   int i, j;
   CProperGeoline proper_geoline;

   // test the proper georects
   for( i = 0; i < m_num_proper_georects; i++ )
   {
      for( j = 0; j < 2; j++ )
      {
         if( geoline.get_proper_geoline( j, proper_geoline ) )
         {
            if( m_proper_georects[i].intersect( proper_geoline ) )
               return TRUE;
         }
      }
   }
   
   return FALSE;
}

// returns TRUE if the two georects overlap
BOOL CGeorect::intersect( const CGeorect& georect ) const
{
   int i, j;
   CProperGeorect proper_georect;

   // test the proper georects
   for( i = 0; i < m_num_proper_georects; i++ )
   {
      for( j = 0; j < 2; j++ )
      {
         if( georect.get_proper_georect( j, proper_georect ) )
         {
            if( m_proper_georects[i].intersect( proper_georect ) )
               return TRUE;
         }
      }
   }
   
   return FALSE;
}

// returns the proper georect with the specified index if it exists
BOOL CGeorect::get_proper_georect( int index, CProperGeorect& proper_georect ) const
{
   if( index < 0 || index >= m_num_proper_georects )
      return FALSE;
   
   proper_georect = m_proper_georects[index];
   return TRUE;
}

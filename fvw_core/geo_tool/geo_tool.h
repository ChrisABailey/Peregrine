// Copyright (c) 1994-2011,2013 Georgia Tech Research Corporation, Atlanta, GA
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
-  FILE NAME:    geo_tool.h
-  LIBRARY NAME: geo_tool
-------------------------------------------------------------------*/

/*------------------------------------------------------------------
-                            Includes
-------------------------------------------------------------------*/

#include "common.h"
#include "geo_tool_d.h"

/*------------------------------------------------------------------
-                           Definitions
-------------------------------------------------------------------*/

#define GEO_MAX_LAT_LON_STRING 40

// GEO_string_to_degrees() error codes
#define SECONDS_ERROR -1
#define MINUTES_ERROR -2
#define DEGREES_ERROR -3
#define OUT_OF_RANGE -4
#define PARSE_ERROR -5
#define DIR_ERROR -6
#define MAGNITUDE_ERROR -7
#define STRING_TOO_LONG -8

#define LAT_SECONDS_ERROR -9
#define LON_SECONDS_ERROR -10
#define LAT_MINUTES_ERROR -11
#define LON_MINUTES_ERROR -12
#define LAT_DEGREES_ERROR -13
#define LON_DEGREES_ERROR -14
#define LAT_OUT_OF_RANGE -15
#define LON_OUT_OF_RANGE -16
#define LAT_PARSE_ERROR -17
#define LON_PARSE_ERROR -18
#define LAT_DIR_ERROR -19
#define LON_DIR_ERROR -20
#define LAT_MAGNITUDE_ERROR -21
#define LON_MAGNITUDE_ERROR -22
#define LAT_STRING_TOO_LONG -23
#define LON_STRING_TOO_LONG -24

// GEO_xy_line_intersect return values
#define INTERSECT_RSLT_YES 0
#define INTERSECT_RSLT_PARALLEL -1   // lines are parallel
#define INTERSECT_RSLT_NO_SEGMENT -2  // line segments do not intersect


/*------------------------------------------------------------------
-                       Function Prototypes 
-------------------------------------------------------------------*/

/* These functions determine if the latitude and longitude represent a valid
   geographic coordinate in their respective units. */
 
GEO_TOOL_DECLSPEC boolean_t GEO_valid_radians(radians_t latitude, radians_t longitude);
GEO_TOOL_DECLSPEC boolean_t GEO_valid_degrees(degrees_t latitude, degrees_t longitude);
GEO_TOOL_DECLSPEC boolean_t GEO_valid_minutes(minutes_t latitude, minutes_t longitude);


/* This function returns TRUE if point_lon is in between left_lon and
   right_lon. A longitude is between left_lon and right_lon if it is
   passed moving eastward from left_lon to right_lon. */

GEO_TOOL_DECLSPEC boolean_t GEO_lon_in_range(double left_lon, double right_lon, double point_lon);

/* Does same as above except that it only checks degree granularity */
GEO_TOOL_DECLSPEC boolean_t GEO_lon_in_deg_range(int left_lon, int right_lon, int point_lon);

/* These functions return a boolean evaluation of the statement, "a is east
   of b." They return FALSE if a=b or a is west of b, and a TRUE if a is east
   of b. */
  
GEO_TOOL_DECLSPEC boolean_t GEO_east_of_radians(radians_t a, radians_t b);
GEO_TOOL_DECLSPEC boolean_t GEO_east_of_degrees(degrees_t a, degrees_t b);
GEO_TOOL_DECLSPEC boolean_t GEO_east_of_minutes(minutes_t a, minutes_t b);

/* These functions return TRUE if the point p is in the geographic
   bounds defined by ur and ll, and they return FALSE if it is completely
   outside the bounds. If the point lies on the bounding rectangle then
   it is considered inside the bounds and the function returns TRUE. */
   
GEO_TOOL_DECLSPEC boolean_t GEO_in_bounds_radians(const r_geo_t& ll, const r_geo_t& ur, const r_geo_t& p);
GEO_TOOL_DECLSPEC boolean_t GEO_in_bounds_degrees(const d_geo_t& ll, const d_geo_t& ur, const d_geo_t& p);
GEO_TOOL_DECLSPEC boolean_t GEO_in_bounds_minutes(const m_geo_t& ll, const m_geo_t& ur, const m_geo_t& p);
GEO_TOOL_DECLSPEC  boolean_t GEO_in_bounds(double ll_lat, double ll_lon,
                        double ur_lat, double ur_lon,
                        double p_lat,  double p_lon);


/* These functions return TRUE if the two regions intersect and they return
   FALSE if they have no intersection. These functions assume that they are
   passed valid inputs. */
   
GEO_TOOL_DECLSPEC boolean_t GEO_intersect_radians(const r_geo_t& ll_A, const r_geo_t& ur_A, 
                                const r_geo_t& ll_B, const r_geo_t& ur_B);
GEO_TOOL_DECLSPEC boolean_t GEO_intersect_degrees(const d_geo_t& ll_A, const d_geo_t& ur_A, 
                                const d_geo_t& ll_B, const d_geo_t& ur_B);
GEO_TOOL_DECLSPEC boolean_t GEO_intersect_minutes(const m_geo_t& ll_A, const m_geo_t& ur_A, 
                                const m_geo_t& ll_B, const m_geo_t& ur_B);
GEO_TOOL_DECLSPEC boolean_t GEO_intersect(double ll_A_lat, double ll_A_lon,
                        double ur_A_lat, double ur_A_lon,
                        double ll_B_lat, double ll_B_lon,
                        double ur_B_lat, double ur_B_lon);

// These functions return TRUE if the circular region A intersects the
// rectangular region B.  These functions assum that they are passed
// valid inputs.  The radius is in meters.
GEO_TOOL_DECLSPEC boolean_t GEO_intersect_degrees(const d_geo_t& center_A, double radius_A, 
   boolean_t great_circle_not_rhumb_line,
   const d_geo_t& ll_B, const d_geo_t& ur_B);
GEO_TOOL_DECLSPEC boolean_t GEO_intersect(degrees_t center_lat_A, degrees_t center_lon_A,
   double radius_A, boolean_t great_circle_not_rhumb_line,
   degrees_t ll_B_lat, degrees_t ll_B_lon,
   degrees_t ur_B_lat, degrees_t ur_B_lon);

/* These functions return TRUE if the region A encloses region B, and
   they return FALSE if not.  These functions assume that they are
   passed valid inputs. */

GEO_TOOL_DECLSPEC boolean_t GEO_enclose_radians(const r_geo_t& ll_A, const r_geo_t& ur_A, 
                              const r_geo_t& ll_B, const r_geo_t& ur_B);
GEO_TOOL_DECLSPEC boolean_t GEO_enclose_degrees(const d_geo_t& ll_A, const d_geo_t& ur_A, 
                              const d_geo_t& ll_B, const d_geo_t& ur_B);
GEO_TOOL_DECLSPEC boolean_t GEO_enclose_minutes(const m_geo_t& ll_A, const m_geo_t& ur_A, 
                              const m_geo_t& ll_B, const m_geo_t& ur_B);
GEO_TOOL_DECLSPEC boolean_t GEO_enclose(double ll_A_lat, double ll_A_lon,
                      double ur_A_lat, double ur_A_lon,
                      double ll_B_lat, double ll_B_lon,
                      double ur_B_lat, double ur_B_lon);

// These functions return TRUE if the circular region A encloses the
// rectangular region B.  These functions assume that they are passed
// valid inputs.  The radius is in meters.
GEO_TOOL_DECLSPEC boolean_t GEO_enclose_degrees(const d_geo_t& center_A, double radius_A,
   boolean_t great_circle_not_rhumb_line,
   const d_geo_t& ll_B, const d_geo_t& ur_B);
GEO_TOOL_DECLSPEC boolean_t GEO_enclose(const degrees_t center_lat_A, degrees_t center_lon_A,
   double radius_A, boolean_t great_circle_not_rhumb_line,
   degrees_t ll_B_lat, degrees_t ll_B_lon, 
   degrees_t ur_B_lat, degrees_t ur_B_lon);

// These functions return TRUE if the rectangular region A encloses the
// circular region B.  These functions assume that they are passed
// valid inputs.  The radius is in meters.
GEO_TOOL_DECLSPEC boolean_t GEO_enclose_degrees(const d_geo_t& ll_A, const d_geo_t& ur_A,
   const d_geo_t& center_B, double radius_B, boolean_t great_circle_not_rhumb_line);
GEO_TOOL_DECLSPEC boolean_t GEO_enclose(degrees_t ll_A_lat, degrees_t ll_A_lon, 
   degrees_t ur_A_lat, degrees_t ur_A_lon,
   degrees_t center_lat_B, degrees_t center_lon_B,
   double radius_B, boolean_t great_circle_not_rhumb_line);

// Given a valid geo-bounds this function computes the width in degrees
// longitude and the height in degrees latitude.
GEO_TOOL_DECLSPEC int GEO_calc_dimensions_degrees(const d_geo_t& ll, const d_geo_t& ur, 
                               degrees_t& geo_width, degrees_t& geo_height);

// Given a center, a width in degrees longitude, and a height in degrees 
// latitude, this function computes the geo-bounds.  If the geo-bounds pass
// the North Pole they will be shifted to the south so that ur.lat is 90.  If
// the geo-bounds pass the South Pole they will be shifted to the north so
// that ll.lat is -90.
GEO_TOOL_DECLSPEC int GEO_center_to_bounds(const d_geo_t& center, degrees_t geo_width, 
   degrees_t geo_height, d_geo_t &ll, d_geo_t &ur);

/* These functions test to see if the given line passes through the given
   geographic window. If any part or all of the line pass through the
   window then the fuction returns TRUE, otherwise it returns FALSE. It
   is REQUIRED that the given line spans less than 180.0 degrees of
   longitude, as the points will be interpreted to define the shortest
   line between the two points. */

GEO_TOOL_DECLSPEC boolean_t GEO_line_test_radians(radians_t window_ll_lat, 
                                radians_t window_ll_lon,
                                radians_t window_ur_lat, 
                                radians_t window_ur_lon,
                                radians_t p1_lat, radians_t p1_lon,
                                radians_t p2_lat, radians_t p2_lon);
GEO_TOOL_DECLSPEC boolean_t GEO_line_test_degrees(degrees_t window_ll_lat, 
                                degrees_t window_ll_lon,
                                degrees_t window_ur_lat, 
                                degrees_t window_ur_lon,
                                degrees_t p1_lat, degrees_t p1_lon,
                                degrees_t p2_lat, degrees_t p2_lon);
GEO_TOOL_DECLSPEC boolean_t GEO_line_test_minutes(minutes_t window_ll_lat,
                                minutes_t window_ll_lon,
                                minutes_t window_ur_lat, 
                                minutes_t window_ur_lon,
                                minutes_t p1_lat, minutes_t p1_lon,
                                minutes_t p2_lat, minutes_t p2_lon);


/* These functions return 0 if the point p is in the geographic
   bounds defined by ur and ll, and they return non-zero if it is completely
   outside the bounds. If the point lies on the bounding rectangle then
   it is considered inside the bounds and the function returns 0.  The
   return value contains four bit flags, N, S, E, and W (N is bit 0, 
   W is bit 3), and they are set to 1 to indicate that the point is north,
   south, east, or west of the box, respectively.  Bits N and S (0 and 1)
   are never both 1, and bits E and W (2 and 3) are never both 1.  If E
   is 1 then the point is east of the lower left longitude by less than
   180 degrees, and if W is 1 then the point is more than 180 degrees east
   of the lower left longitude. */

GEO_TOOL_DECLSPEC int GEO_bounds_check_radians(radians_t ll_lat, 
                             radians_t ll_lon,
                             radians_t ur_lat, 
                             radians_t ur_lon,
                             radians_t p_lat,
                             radians_t p_lon);
GEO_TOOL_DECLSPEC  int GEO_bounds_check_degrees(degrees_t ll_lat, 
                             degrees_t ll_lon,
                             degrees_t ur_lat, 
                             degrees_t ur_lon,
                             degrees_t p_lat, 
                             degrees_t p_lon);
GEO_TOOL_DECLSPEC int GEO_bounds_check_minutes(minutes_t ll_lat,
                             minutes_t ll_lon,
                             minutes_t ur_lat, 
                             minutes_t ur_lon,
                             minutes_t p_lat, 
                             minutes_t p_lon);

// Given three points x, y, and z, this function computes the distance from
// x to y, the distance from y to z, and the angle XYZ, where y is at the
// vertex.  The computation can be done either Great Circle or Rhumb-line.
// The returned angle will always be between 0 and 180 degrees.
GEO_TOOL_DECLSPEC int GEO_calc_lengths_and_angle(const d_geo_t& x, const d_geo_t& y, const d_geo_t& z, 
   boolean_t great_circle_not_rhumb_line, double &XY, double &YZ, 
   double &angle);

// Given three points x, y, and z, this function computes the distance from
// x to y, the distance from y to z, and the angle XYZ, where y is at the
// vertex.  The computation can be done either Great Circle or Rhumb-line.
// The angle will be negative if the angle is clockwise, and positive if
// the angle is counter clockwise.  The returned angle will be between
// -180 and +180 degrees.
GEO_TOOL_DECLSPEC int GEO_calc_lengths_and_angle_ex(const d_geo_t& x, const d_geo_t& y, const d_geo_t& z, 
   boolean_t great_circle_not_rhumb_line, double &XY, double &YZ, 
   double &angle);

// Calculate the distance in meters and the bearing in degrees.  If 
// great_circle_not_rhumb_line is TRUE, then the great circle path 
// will be used, otherwise the rhumb_line path will be used.
GEO_TOOL_DECLSPEC int GEO_calc_range_and_bearing(const d_geo_t& start, const d_geo_t& end, 
   double &distance, double &bearing, boolean_t great_circle_not_rhumb_line);

GEO_TOOL_DECLSPEC int GEO_calc_range_and_bearing(  double lat1, double lon1, double lat2, double lon2, 
                                 double *distance, double *bearing, 
                                 boolean_t great_circle_not_rhumb_line);

// Calculate the end point from the start point, the distance in meters,
// and the bearing in degrees.  If great_circle_not_rhumb_line is TRUE, then
// the great circle path will be used, otherwise the rhumb_line path will be
// used.
GEO_TOOL_DECLSPEC int GEO_calc_end_point(const d_geo_t& start, double distance, double bearing,
   d_geo_t &end, boolean_t great_circle_not_rhumb_line);

GEO_TOOL_DECLSPEC int GEO_calc_end_point( double lat1, double lon1, double distance, double bearing,
                        double *lat2, double *lon2, 
                        boolean_t great_circle_not_rhumb_line);

// Since the Mercator projection can not handle the poles, you have to move
// points at the poles slightly off the pole in order to be able to use rhumb
// line range and bearing functions and the like.  This function will fudge a
// latitude for you as needed.  It returns TRUE if the value is changed, FALSE
// otherwise.
GEO_TOOL_DECLSPEC boolean_t GEO_fudge_polar_lat_for_rhumb_line(degrees_t &lat);

GEO_TOOL_DECLSPEC int GEO_distance(degrees_t pt1_lat, degrees_t pt1_lon, degrees_t pt2_lat,
   degrees_t lt2_lon, double* distance_in_kilometers,
   degrees_t* angle_between_points);


GEO_TOOL_DECLSPEC  int GEO_geo_to_distance ( double lat1, double lng1,   // IN  - geo1 in degrees
                          double lat2, double lng2,   // IN  - geo2 in degrees
                          double      *Distance,      // OUT - distance in meters
                          double      *Angle );       // OUT - bearing in degrees

GEO_TOOL_DECLSPEC int GEO_distance_to_geo( double startlat, double startlng, // IN  - start geo in degrees
                         double distance,                  // IN  - distance in meters
                         double azimuth,                   // IN  - bearing in degrees
                         double* lat, double *lng);        // OUT - new geo in degrees

GEO_TOOL_DECLSPEC int GEO_magnetic_variation(double dlat, double dlon,  // IN  - in degrees
                     int year,               // IN  - e.g.  1995 
                            int month,                  // IN  - e.g.   3 (= March)
                            int altitude,               // IN  - altitude in meters
                     double *magvar);        // OUT - in degrees, + = East, - = West

GEO_TOOL_DECLSPEC int GEO_current_magnetic_variation( double dlat, double dlon, 
                           int altitude,  // IN  - altitude in meters
                           double *magvar);

class GEO_TOOL_DECLSPEC Mercator 
{
private:
   degrees_t m_center_lon;
   double m_R;

public:
   Mercator(degrees_t center_lon, double scale = 1.0);
   virtual ~Mercator() {}
   double lon_to_x(degrees_t lon);
   double lat_to_y(degrees_t lat);
   degrees_t x_to_lon(double x);
   degrees_t y_to_lat(double y);
   double get_min_x();
   double get_max_x();
   void set_center_lon(double lon);
   degrees_t get_center_lon() { return m_center_lon; }
}; // End Mercator


// These functions convert Latitudes and Longitudes from string to degrees,
// and vice-versa.  The lat_not_lon parameter indicates that the value in
// degrees is a latitude when it is TRUE, and a longitude when it is FALSE. 

GEO_TOOL_DECLSPEC void GEO_degrees_to_string(degrees_t degrees, boolean_t lat_not_lon, 
   char *string, int string_len);

// This function can return a number of error codes listed above. 

GEO_TOOL_DECLSPEC int GEO_string_to_degrees(const char *string, boolean_t lat_not_lon, 
   degrees_t *degrees);

// The error codes from GEO_string_to_degrees() can be passed to this function
// to inform the user that of the conversion error. 

GEO_TOOL_DECLSPEC void GEO_report_string_to_degrees_error(int error, boolean_t lat_not_lon);


// --------------------------------------------------------------------------
// Geographical functions (GEO)
//
// Programmer:  Alfredo Bencomo
//
// Date:        18Oct96
// --------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// Given the WGS-84 latitude and longitude; return a formatted georef
// coordinate string according to display format of the default display:
// DEGREES, DEGREES_MINUTES, DEGREES_MINUTES_SECONDS, or MILGRID.
//
GEO_TOOL_DECLSPEC int GEO_lat_lon_to_string(degrees_t lat, degrees_t lon, char *geo_string, int geo_string_len);

#ifdef _WIN32
GEO_TOOL_DECLSPEC CString GEO_get_formatted_location_string(degrees_t lat, degrees_t lon);
#endif

/////////////////////////////////////////////////////////////////////////////
// Given the WGS-84 latitude, longitude, and degrees per pixel; return a
// formatted georef coordinate string with precision according to dpp and
// format according to display format of the default display: DEGREES,
// DEGREES_MINUTES, DEGREES_MINUTES_SECONDS, or MILGRID.
//
GEO_TOOL_DECLSPEC int GEO_lat_lon_to_string(degrees_t lat, degrees_t lon, degrees_t dpp,
   char *geo_string, int geo_string_len);


/////////////////////////////////////////////////////////////////////////////
// Given a georef coordiante string (milgrig or latitude/longitude) and a
// datum; calculate latitide and longitude
//
GEO_TOOL_DECLSPEC int GEO_string_to_lat_lon(const char *geo_string, const char *datum, 
   degrees_t *lat, degrees_t *lon);

/////////////////////////////////////////////////////////////////////////////
// Retrieve the datum index associated with a particular datum code
//
GEO_TOOL_DECLSPEC int GEO_datum_valid(const char *datum, int &index_datum);

/////////////////////////////////////////////////////////////////////////////
// GEO's setting functions - GEODLL.INI
//

// Returns the datum for the default display: primary or secondary.
// See GEO_get_default_display().  The datum string will contain 5
// characters, plus 1 for '\0'.
GEO_TOOL_DECLSPEC void GEO_get_default_datum(char *datum, int datum_len);

// Returns the datum for the primary display format.
// See GEO_get_default_display().  The datum string will contain 5
// characters, plus 1 for '\0'.
GEO_TOOL_DECLSPEC void GEO_get_primary_datum(char *value, int value_len);

// Returns the datum for the secondary display format.
// See GEO_get_default_display().  The datum string will contain 5
// characters, plus 1 for '\0'.
GEO_TOOL_DECLSPEC void GEO_get_secondary_datum(char *value, int value_len);

// Returns the location format for the primary display format.
// See GEO_get_default_display().  The datum string will contain 20
// characters, plus 1 for '\0'.
GEO_TOOL_DECLSPEC void GEO_get_primary_format(char *value, int value_len);

// Returns the location format for the secondary display format.
// See GEO_get_default_display().  The datum string will contain 20
// characters, plus 1 for '\0'.
GEO_TOOL_DECLSPEC void GEO_get_secondary_format(char *value, int value_len);

// Returns "PRIMARY" if the primary display format is in use.  Returns
// "SECONDARY" if the secondary display format is in use.
GEO_TOOL_DECLSPEC void GEO_get_default_display(char *value, int value_len);

GEO_TOOL_DECLSPEC int  GEO_set_primary_datum(const char *value);

GEO_TOOL_DECLSPEC int  GEO_set_secondary_datum(const char *value);

GEO_TOOL_DECLSPEC int  GEO_set_primary_format(const char *value);

GEO_TOOL_DECLSPEC int  GEO_set_secondary_format(const char *value);

GEO_TOOL_DECLSPEC int  GEO_set_default_display(const char *value);

GEO_TOOL_DECLSPEC void GEO_get_primary_lat_lon_format(char *value, int value_len);

GEO_TOOL_DECLSPEC void GEO_get_secondary_lat_lon_format(char *value, int value_len);

GEO_TOOL_DECLSPEC int GEO_set_primary_lat_lon_format(const char *value);

GEO_TOOL_DECLSPEC int GEO_set_secondary_lat_lon_format(const char *value);

// *****************************************************************

// This function projects a lat/long into Gnomonic projection space
// This azimuthal projection has the very interest property that
// great circle lines are straight lines
// NOTE: 1. This function is only valid for less than half a hemisphere
//          In other words (lat, lon) must be less than 90 degrees from
//          (centerlat, centerlon)


GEO_TOOL_DECLSPEC boolean_t GEO_gnomonic_geo_to_xy(
      double lat, double lon,                // geo to convert (degrees)
      double center_lat, double center_lon,  // center of projection (degrees)
      double *x, double *y);                 // OUT -- xy in projection

// *****************************************************************

// This function converts an xy pair into a lat/long in 
// Gnomonic projection space.  This azimuthal projection has the very 
// interest property that great circle lines are straight lines
// NOTE: 1. This function is only valid for less than half a hemisphere
//          In other words (lat, lon) must be less than 90 degrees from
//          (centerlat, centerlon)

GEO_TOOL_DECLSPEC boolean_t GEO_gnomonic_xy_to_geo(
      double x, double y,                    // xy to convert
      double center_lat, double center_lon,  // center of projection (degrees)
      double *lat, double *lon);             // OUT -- geo in projection (degrees)

// *****************************************************************

// This function computes the intersection of two line segments.  
// (ix, iy) will contain the intersection point of the actual or extended segments
//
// return values:
//     INTERSECT_RSLT_YES         segments intersect, ix/iy valid
//     INTERSECT_RSLT_NO_SEGMENT  segments do not intersect, but lines that
//                                they define do, ix/iy valid
//     INTERSECT_RSLT_PARALLEL    segments are collinear. ix/iy invalid
//     

GEO_TOOL_DECLSPEC int GEO_xy_line_intersection(
                        double ax, double ay,      //line 1 start point
                        double bx, double by,      //line 1 end point
                        double cx, double cy,      //line 2 start point
                        double dx, double dy,      //line 2 end point
                        double *ix, double *iy);   // intersection point

// *****************************************************************

// This function calculates the intersection of two great circle line segments
// All parameters are in degrees
// NOTES:  1. the function only calculates the closest intersection of the
//            great circle lines that the segments define
//         2. the function only works correctly if the coords 2,3,4, and the
//            the resultant (ilat, ilon) are no more than 90 degrees from 
//            coord 1
//         3. even if the line segments do not intersect, the intersection
//            of the great circle lines that they define is returned

GEO_TOOL_DECLSPEC boolean_t GEO_great_circle_intersection(
      double lat1, double lon1,     // starting point of first line
      double lat2, double lon2,     // ending point of first line
      double lat3, double lon3,     // starting point of second line
      double lat4, double lon4,     // ending point of second line
      double *ilat, double *ilon);  // intersection of two geo lines

// *****************************************************************

// These functions return the difference between the latitudes or
// longitudes given.
// NOTES:  1. the functions ALWAYS return a positive value
//         2. all values passed in are assumed to be correct; there is
//            no error-checking done on them
//         3. the functions always calculate the shortest difference
//            i.e. GEO_delta_lon would return 40 degrees instead of 320

GEO_TOOL_DECLSPEC degrees_t GEO_delta_lat(
      degrees_t lat1,         // latitude of first point
      degrees_t lat2);        // latitude of second point

GEO_TOOL_DECLSPEC degrees_t GEO_delta_lon(
      degrees_t lon1,         // longitude of first point
      degrees_t lon2);        // longitude of second point


// *****************************************************************

// These functions convert between geographic coordinates and Cartesian XYZ

GEO_TOOL_DECLSPEC int GEO_geo_to_xyz(double lat, double lon, double height, 
               double *x, double *y, double *z);

GEO_TOOL_DECLSPEC int GEO_xyz_to_geo(double x, double y, double z, 
               double *lat, double *lon, double *height);


// *****************************************************************

// this function takes an array of geo coords and computes the "center of mass"
GEO_TOOL_DECLSPEC int GEO_center_of_mass(d_geo_t *geo, int numpts, d_geo_t *center_geo);


// *****************************************************************


// A proper geoline has a left endpoint and a right endpoint and does not
// cross the international date line. So, the left endpoint longitude is
// always <= the right point longitude.
class GEO_TOOL_DECLSPEC CProperGeoline
{
public:
   CProperGeoline( );
   CProperGeoline( degrees_t left_lat, degrees_t left_lon, degrees_t right_lat, degrees_t right_lon );
   ~CProperGeoline( );
   void set( degrees_t left_lat, degrees_t left_lon, degrees_t right_lat, degrees_t right_lon );
   void get( degrees_t &left_lat, degrees_t &left_lon, degrees_t &right_lat, degrees_t &right_lon ) const;
   BOOL longitude_at_latitude( degrees_t lat, degrees_t &lon ) const;
   BOOL latitude_at_longitude( degrees_t lon, degrees_t &lat ) const;

protected:
   degrees_t m_left_lat, m_left_lon, m_right_lat, m_right_lon;
};

// A geoline has a left endpoint and a right endpoint and can
// cross the international date line. If so, the left endpoint longitude is
// greater than the right point longitude. Internally, the geoline is stored 
// as one or two propper geolines.
class GEO_TOOL_DECLSPEC CGeoline
{
public:
   CGeoline( );
   CGeoline( degrees_t left_lat, degrees_t left_lon, degrees_t right_lat, degrees_t right_lon );
   ~CGeoline( );
   void set( degrees_t left_lat, degrees_t left_lon, degrees_t right_lat, degrees_t right_lon );
   void get( degrees_t &left_lat, degrees_t &left_lon, degrees_t &right_lat, degrees_t &right_lon ) const;
   BOOL longitude_at_latitude( degrees_t lat, degrees_t &lon ) const;
   BOOL latitude_at_longitude( degrees_t lon, degrees_t &lat ) const;
   BOOL get_proper_geoline( int index, CProperGeoline& proper_geoline ) const;

protected:
   degrees_t m_left_lat, m_left_lon, m_right_lat, m_right_lon;
   int m_num_proper_geolines;
   CProperGeoline m_proper_geolines[2];
};

// A proper georect is a rectangular region which does not cross the international
// date line. It is defined by its max/min lat and lon.
class GEO_TOOL_DECLSPEC CProperGeorect
{
public:
   CProperGeorect( );
   CProperGeorect( degrees_t min_lat, degrees_t min_lon, degrees_t max_lat, degrees_t max_lon );
   ~CProperGeorect( );
   void set( degrees_t min_lat, degrees_t min_lon, degrees_t max_lat, degrees_t max_lon );
   void get( degrees_t &min_lat, degrees_t &min_lon, degrees_t &max_lat, degrees_t &max_lon ) const;
   BOOL intersect( degrees_t lat, degrees_t lon ) const;
   BOOL intersect( const CProperGeoline& proper_geoline ) const;
   BOOL intersect( const CProperGeorect& proper_georect ) const;

protected:
   degrees_t m_min_lat, m_min_lon, m_max_lat, m_max_lon;
};

// A georect is a rectangular region which can cross the international
// date line. It is defined by its lower left and upper right lat and lon.
// Internally, the georect id stored as one or two proper georects.
class GEO_TOOL_DECLSPEC CGeorect
{
public:
   CGeorect( );
   CGeorect( degrees_t ll_lat, degrees_t ll_lon, degrees_t ur_lat, degrees_t ur_lon );
   ~CGeorect( );
   void set( degrees_t ll_lat, degrees_t ll_lon, degrees_t ur_lat, degrees_t ur_lon );
   void get( degrees_t &ll_lat, degrees_t &ll_lon, degrees_t &ur_lat, degrees_t &ur_lon ) const;
   BOOL intersect( degrees_t lat, degrees_t lon ) const;
   BOOL intersect( const CGeoline& geoline ) const;
   BOOL intersect( const CGeorect& georect ) const;
   BOOL get_proper_georect( int index, CProperGeorect& proper_georect ) const;

protected:
   degrees_t m_ll_lat, m_ll_lon, m_ur_lat, m_ur_lon;
   int m_num_proper_georects;
   CProperGeorect m_proper_georects[2];
};

/////////////////////////////////////////////////////////////////////////////
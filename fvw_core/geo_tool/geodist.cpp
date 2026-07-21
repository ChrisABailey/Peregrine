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



// geodist.cpp

#include "stdafx.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "geo_tool.h"

#define RAD_TO_METERS(radians)  (((double)(radians)) *  WGS84_R2_METERS)
#define METERS_TO_RAD(meters)   (((double)(meters)) *  (1.0 / WGS84_R2_METERS))
                                                       

// Calculates the rhumb line distance (meters) and bearing (degrees) between
// the two points.  Neither point may be a pole.
static int calc_rhumb_line_distance(double lat1, double lon1,
   double lat2, double lon2, double *distance, double *heading); 

// Calculate the end point from the starting point, the distance (meters),
// and the bearing (degrees).
static int calc_rhumb_line_inverse(double lat1,double lon1, 
   double distance, double heading, double *lat2,double *lon2);   

//*****************************************************************************

// Given three points x, y, and z, this function computes the distance from
// x to y, the distance from y to z, and the angle XYZ, where y is at the
// vertex.  The computation can be done either Great Circle or Rhumb-line.
// The returned angle will always be between 0 and 180 degrees.
int GEO_calc_lengths_and_angle(const d_geo_t& x, const d_geo_t& y, const d_geo_t& z, 
   boolean_t great_circle_not_rhumb_line, double &XY, double &YZ, 
   double &angle)
{
   double temp;

   if (GEO_calc_lengths_and_angle_ex(x, y, z, great_circle_not_rhumb_line,
      XY, YZ, temp) != SUCCESS)
      return FAILURE;

   if (temp < 0.0)
      angle = -temp;
   else
      angle = temp;

   return SUCCESS;
}

//*****************************************************************************

// Given three points x, y, and z, this function computes the distance from
// x to y, the distance from y to z, and the angle XYZ, where y is at the
// vertex.  The computation can be done either Great Circle or Rhumb-line.
// The angle will be negative if the angle is clockwise, and positive if
// the angle is counter clockwise.  The returned angle will be between
// -180 and +180 degrees.
int GEO_calc_lengths_and_angle_ex(const d_geo_t& x, const d_geo_t& y, const d_geo_t& z, 
   boolean_t great_circle_not_rhumb_line, double &XY, double &YZ, 
   double &angle)
{
   double rx, rz, bx, bz;

   // compute the range and bearing from y to x and the range and bearing
   // from y to z
   if (GEO_calc_range_and_bearing(y, x, rx, bx, great_circle_not_rhumb_line) != 
      SUCCESS || 
      GEO_calc_range_and_bearing(y, z, rz, bz, great_circle_not_rhumb_line) != 
      SUCCESS)
   {
      ERR_report("GEO_calc_range_and_bearing() failed.");
      return FAILURE;
   }

   // compute the angle from the difference in the bearing from y to x and
   // the bearing from y to z.
   angle = bx - bz;
   if (angle > 180.0)
      angle = angle - 360.0;
   else if (angle < -180.0)
      angle = 360.0 + angle;

   ASSERT(angle >= -180.0 && angle <= 180.0);

   XY = rx;
   YZ = rz;

   return SUCCESS;
}

//*****************************************************************************

// Calculate the distance in meters and the bearing in degrees.  If 
// great_circle_not_rhumb_line is TRUE, then the great circle path 
// will be used, otherwise the rhumb_line path will be used.
int GEO_calc_range_and_bearing(const d_geo_t& start, const d_geo_t& end, 
   double &distance, double &bearing, boolean_t great_circle_not_rhumb_line)
{
   int status;

   if (great_circle_not_rhumb_line)
      status = GEO_geo_to_distance(start.lat, start.lon, end.lat, end.lon,
         &distance, &bearing);
   else
      status =  calc_rhumb_line_distance(start.lat, start.lon, end.lat, end.lon,
         &distance, &bearing);

   return status;
}

//*****************************************************************************
//*****************************************************************************

// Calculate the distance in meters and the bearing in degrees.  If 
// great_circle_not_rhumb_line is TRUE, then the great circle path 
// will be used, otherwise the rhumb_line path will be used.

int GEO_calc_range_and_bearing(  double lat1, double lon1, double lat2, double lon2, 
                                 double *distance, double *bearing, 
                                 boolean_t great_circle_not_rhumb_line)
{
   int status;

   if (great_circle_not_rhumb_line)
      status = GEO_geo_to_distance(lat1, lon1, lat2, lon2,
         distance, bearing);
   else
      status =  calc_rhumb_line_distance(lat1, lon1, lat2, lon2,
         distance, bearing);

   return status;
}

//*****************************************************************************

// Calculate the end point from the start point, the distance in meters,
// and the bearing in degrees.  If great_circle_not_rhumb_line is TRUE, then
// the great circle path will be used, otherwise the rhumb_line path will be
// used.
int GEO_calc_end_point(const d_geo_t& start, double distance, double bearing,
   d_geo_t &end, boolean_t great_circle_not_rhumb_line)
{
   int status;

   if (great_circle_not_rhumb_line)
      status = GEO_distance_to_geo(start.lat, start.lon, distance, bearing,
         &end.lat, &end.lon);
   else
      status = calc_rhumb_line_inverse(start.lat, start.lon, distance, bearing,
         &end.lat, &end.lon);

   return status;
}



// *********************************************************************
//*****************************************************************************

// Calculate the end point from the start point, the distance in meters,
// and the bearing in degrees.  If great_circle_not_rhumb_line is TRUE, then
// the great circle path will be used, otherwise the rhumb_line path will be
// used.

int GEO_calc_end_point( double lat1, double lon1, double distance, double bearing,
                        double *lat2, double *lon2, 
                        boolean_t great_circle_not_rhumb_line)
{
   int status;

   if (great_circle_not_rhumb_line)
      status = GEO_distance_to_geo(lat1, lon1, distance, bearing,
         lat2, lon2);
   else
      status = calc_rhumb_line_inverse(lat1, lon1, distance, bearing,
                                       lat2, lon2);

   return status;
}

// *********************************************************************

// Calculates the rhumb line distance (meters) and bearing (degrees) between
// the two points.  Neither point may be a pole.
int calc_rhumb_line_distance(double lat1, double lon1,
                             double lat2, double lon2, 
                             double *distance, double *heading)
{
   // Fudge the end points at the poles.  If either end-point is within so
   // close to the poll that it will choke the tan() function below, they have 
   // to be moved.  In this case we will treat the rhumb line as a line of
   // longitude, because the fudge factor only kicks in if the latitude falls
   // within a 5 inches of a pole.
   if (GEO_fudge_polar_lat_for_rhumb_line(lat1))
      lon1 = lon2;
   if (GEO_fudge_polar_lat_for_rhumb_line(lat2))
      lon2 = lon1;

   // convert coordinates to radians
   double rad_lat1 = DEG_TO_RAD(lat1);
   double rad_lon1 = DEG_TO_RAD(lon1);
   double rad_lat2 = DEG_TO_RAD(lat2);
   double rad_lon2 = DEG_TO_RAD(lon2);
   double q;

   // Compute dX in a mercator projection with a scale factor of 
   // 1 / WGS84_R2_METERS.  Must handle IDL crossing.
   double dX = rad_lon2 - rad_lon1;
   if (dX > PI)
      dX -= TWO_PI;
   else if (dX < -PI)
      dX += TWO_PI;

   // Compute dY in a mercator projection with a scale factor of 
   // 1 / WGS84_R2_METERS.
   double dY = log(tan(rad_lat2/2+PI/4)/tan(rad_lat1/2+PI/4));

   // Compute the inverse of the mercator projection stretching factor.
   if(fabs(rad_lat2-rad_lat1) < sqrt(1.0e-15))
      q = cos(rad_lat1);
   else
      q = (rad_lat2-rad_lat1)/dY;

   // compute the distance adjusted for distortion
   *distance = RAD_TO_METERS(sqrt(q*q*dX*dX+(rad_lat2-rad_lat1)*(rad_lat2-rad_lat1)));

   // Since the mercator projection does not distort heading, it is the 
   // atan(dX/dY).
   *heading = RAD_TO_DEG(atan2(dX,dY));

   // Correct the heading to be a clockwise angle between 0 and 360, relative
   // to the dY axis.  Note atan2 returns a value between +/- PI based on the
   // values of dX and dY.  The angle is clockwise relative to the dY axis,
   // as dX is the opposite side and dY is the adjacent side.
   if (*heading < 0)
      *heading = 360 + *heading;
   ASSERT(*heading >= 0 && *heading <= 360);

   return SUCCESS;
}

// *********************************************************************

// Calculate the end point from the starting point, the distance (meters),
// and the bearing (degrees).  The starting point can not be a pole.
int calc_rhumb_line_inverse(double lat1,double lon1, 
                            double distance, double heading,
                            double *lat2,double *lon2)
{
   double rad_lat1, rad_lon1;
   double rad_lat2, rad_lon2;
   double rad_distance;
   double rad_heading;
   double dY, dX, q;

   // Fudge the starting point at the poles.
   GEO_fudge_polar_lat_for_rhumb_line(lat1);

   // convert point 1 to radians
   rad_lat1 = DEG_TO_RAD(lat1);
   rad_lon1 = DEG_TO_RAD(lon1);

   // input is in meters and degrees
   rad_distance = METERS_TO_RAD(distance);
   rad_heading = DEG_TO_RAD(heading);

   // compute ending latitude
   rad_lat2 =  rad_lat1+rad_distance*cos(rad_heading);

   // Compute dY in a mercator projection with a scale factor of 
   // 1 / WGS84_R2_METERS.
   dY = log(tan(rad_lat2/2+PI/4)/tan(rad_lat1/2+PI/4));

   // Compute the inverse of the mercator projection stretching factor.
   if(fabs(rad_lat2-rad_lat1) < sqrt(1.0e-15))
      q = cos(rad_lat1);
   else
      q = (rad_lat2-rad_lat1)/dY;
   
   // Compute dX in a mercator projection with a scale factor of 
   // 1 / WGS84_R2_METERS.
   dX = rad_distance * sin(rad_heading)/q;

   // Compute ending longitude.  Must handle IDL crossing.
   rad_lon2 = rad_lon1 + dX;
   if (rad_lon2 > PI)
      rad_lon2 -= TWO_PI;
   else if (rad_lon2 < -PI)
      rad_lon2 += TWO_PI;

   *lat2 = RAD_TO_DEG(rad_lat2);
   *lon2 = RAD_TO_DEG(rad_lon2);

   return SUCCESS;
}

// *********************************************************************
// *********************************************************************

//   notes: 1) exceeding input range of -1 through +1 will cause runtime error 
//          2) only returns principal values                                   
//               ( 0 through pi radians ) ( 0 through +180 degrees )           

double ArcCos (double InValue)
{
   double  Result;
   double tmpacos;

   if (fabs(InValue) > 1.0)
   {
      return 0.0;
   }

   if (InValue == 0.0)
      tmpacos = HALF_PI;
   else
   {
      Result = atan( sqrt( 1.0 - InValue * InValue ) / InValue );
      if (InValue < 0.0)
         tmpacos = Result + PI;
      else
         tmpacos = Result;
   }
   return tmpacos;
}  // end of  ArcCos 

/*************************************************************************************/
/* From latitude & longitude of two points (p1 & p2) at point 1,                     */
/* compute bearing(=azimuth) & distance.                                             */
/*                                                                                   */
/* The algorithm was published by T.Vincenty in Survey Review, NO 176, 1975          */
/* VOL XXIII Pages 88-93.                                                            */
/* It has also been used by Defense Mapping Agency Systems Center                    */
/* 6500 Brookes Lane Washington, D.C. 20315-0030 (POC: Bradford W. Drew - SC/EG)     */
/* DMA's version is written in Fortran.                                              */
/*************************************************************************************/
int GEO_geo_to_distance (double pt1Lat, double pt1Lon, 
                           double pt2Lat, double pt2Lon,
                           double far *mag,  // distance in meters
                           double far *dir)  // bearing
{
   const long double A     = 6378137.0;      /* semi-major axis of ellipsoid */
   const long double RECF  = 298.257223563;  /* reciprocal of flattening (1/F) */

   const long double TESTV = 1.0E-11;
   
   long double /* intermediate values used in the calculation */
            B, C2SIGM, CDLAMS, COSSAZ, COSSIG, COSU1, COSU2,
            DENOM, DLAM, DLAMS, FL, RNUMER,
            SDLAMS, SIG, SINAZ, SINSIG, SINU1, SINU2,
            TA, TANU1, TANU2, TB, TC, TEMP,US;
   int ITER, cnt;

   // set default values for return params
   *mag = 0.0;
   *dir = 0.0;

   // check for valid input values
   if ((pt1Lat < -90.0) || (pt1Lat > 90.0))
      return FAILURE;
   if ((pt2Lat < -90.0) || (pt2Lat > 90.0))
      return FAILURE;
   if ((pt1Lon < -180.0) || (pt1Lon > 180.0))
      return FAILURE;
   if ((pt2Lon < -180.0) || (pt2Lon > 180.0))
      return FAILURE;

   // convert to radians 
   pt1Lat = DEG_TO_RAD(pt1Lat);
   pt1Lon = DEG_TO_RAD(pt1Lon);
   pt2Lat = DEG_TO_RAD(pt2Lat);
   pt2Lon = DEG_TO_RAD(pt2Lon);

   /* adjust values */
   if (pt1Lon > PI) 
      pt1Lon = pt1Lon - 2.0 * PI;
   if (pt1Lon < -PI) 
      pt1Lon = pt1Lon + 2.0 * PI;
   if (pt2Lon > PI) 
      pt2Lon = pt2Lon - 2.0 * PI;
   if (pt2Lon < -PI) 
      pt2Lon = pt2Lon + 2.0 * PI;

   if( fabs( pt1Lat - pt2Lat ) < TESTV     &&
                 fabs( pt1Lon - pt2Lon ) < TESTV )
   {
      /* TWO STATIONS ARE IDENTICAL 
         SET DISTANCE & AZIMUTHS TO ZERO */
        *mag = 0.00;
      *dir = 0.00;
      return SUCCESS;
   }

   /* SEMI MAJOR AXIS - B */
   B = A*( RECF -1.0 ) / RECF;

   /* FLATTENING (F) */
   FL = 1.0 / RECF;

   /* ITERATION COUNTER */
   ITER = 0;

   /* TANGENT OF REDUCED LATITUDE (U) OF POINT 1 & 2 */
   TANU1 = ( 1.0 -FL ) * sin( pt1Lat ) / cos( pt1Lat );
   TANU2 = ( 1.0 -FL ) * sin( pt2Lat ) / cos( pt2Lat );

   /* COSINE & SINE OF U1 & U2 FROM TRIG IDENTITIES */
   COSU1 = 1.0 / sqrt( 1.0 + pow(TANU1,2) );
   SINU1 = TANU1 * COSU1;
   COSU2 = 1.0 / sqrt( 1.0 + pow(TANU2,2) );
   SINU2 = TANU2 * COSU2;

   /* SET SINU1 TO ZERO IF COSU1 IS ONE */
   if( COSU1 == 1.0 ) 
      SINU1 = 0.00;

   /* SET SINU2 TO ZERO IF COSU2 IS ONE */
   if( COSU2 == 1.0 ) 
      SINU2 = 0.00;

   /* DIFFERENCE IN LO->GITUDE */
   DLAM = pt2Lon - pt1Lon;

   /* 1ST ESTIMATE DLAMS - DIFF IN LONGITUDE ON AUXILIARY SPHERE */
   DLAMS = DLAM;

   do {

      /* SINE & COSINE OF DLAMS */
      SDLAMS = sin( DLAMS );
      CDLAMS = cos( DLAMS );

      /* SINE & COSINE OF SIGMA */
      SINSIG = sqrt(   pow(( COSU2*SDLAMS ),2)
                     + pow(( COSU1*SINU2  -SINU1*COSU2*CDLAMS ),2 ));
      COSSIG = SINU1*SINU2 +COSU1*COSU2*CDLAMS;

      /* SIGMA */
      SIG = atan2( SINSIG, COSSIG );

      /* SINE OF AZIMUTH (AZ) OF GEODESIC AT EQUATOR */
      SINAZ = COSU1*COSU2*SDLAMS/SINSIG;

      /* COSINE SQUARED OF AZ USING TRIG IDENTITY */
      COSSAZ = 1.0 - pow(SINAZ,2);

       /* COSINE OF 2 SIGMA-SUB-M */
      if ( SINU1 == 0.0  || SINU2 == 0.0 )
         C2SIGM = COSSIG;
      else
         C2SIGM = COSSIG -2.0*SINU1*SINU2/COSSAZ;

      /* TERM C */
      TC = FL*COSSAZ*( 4.0 +FL*( 4.0 -3.0*COSSAZ ) )/16.0;

      /* SAVE PREVIOUS DIFF IN LONGITUDE ON AUXILIARY SPHERE */
      TEMP = DLAMS;

      /* NEWEST DIFFERENCE IN LONGITUDE ON AUXILIARY SPHERE */
      DLAMS = DLAM  +( 1.0 -TC )*FL*SINAZ*( SIG
                     +TC*SINSIG*( C2SIGM +TC*COSSIG*( -1.0
                     +2.0*pow(C2SIGM,2) ) ) );

      /* ITERATION COUNTER */
      ITER = ITER +1;

      /* TEST FOR NEARLY ANTIPODAL LINE CONDITION */
      if( fabs( DLAMS ) > PI && ITER > 50 )
      {   /* ANTIPODAL CONDITION */
         *mag = 0.0;
         *dir = 0.0;
         return FAILURE;
      }

  } while ( fabs(TEMP-DLAMS) > TESTV );


   /* SMALL u SQUARED (US) */
   US = COSSAZ*( pow(A,2) -pow(B,2))/pow(B,2);

   /* FORWARD AZIMUTH FROM NORTH */
   /* NUMERATOR & DENOMINATOR */
   RNUMER =  COSU2*SDLAMS;
   DENOM =  COSU1*SINU2 -SINU1*COSU2*CDLAMS;
   *dir = atan2( RNUMER, DENOM );
   if ( *dir < 0.0 ) 
      *dir = *dir + 2.0 * PI;

   /* TERM A */
   TA = 1.0 +US*( 4096.0 + US *( -768.0
                          + US *( 320.0 -175.0*US ) ) ) / 16384.0;

   /* TERM B */
   TB = US*( 256.0 +US*( -128.0 +US*( 74.0 -47.0*US ) ) )/1024.0;

   /* GEODETIC DISTANCE */
   *mag  = B * TA *( SIG  - TB * SINSIG *( C2SIGM
               + TB *( COSSIG*( -1.0 +2.0*pow(C2SIGM,2))
               - TB* C2SIGM*(   -3.0 +4.0*pow(SINSIG,2))*(
                    -3.0 +4.0*pow(C2SIGM,2))/6.0 )/4.0  )   );

// *mag *= FT_per_METER;  /* convert to feet */

   cnt = 0;
   while (*dir < 0.0)
   {
      *dir += (2*PI);
      cnt++;
      // too many iterations here indicate a bad value of *dir
      if (cnt > 10)
         return FAILURE;
   }
   cnt = 0;
   while (*dir > (2*PI))
   {
      *dir -= (2*PI);
      cnt++;
      // too many iterations here indicate a bad value of *dir
      if (cnt > 10)
         return FAILURE;
   }

   if (*mag < 0.000000001) 
      *dir = 0.0;
  
   /* convert to degrees */
   *dir = RAD_TO_DEG(*dir);

   return SUCCESS;
}
// end of GEO_geo_to_distance

int GEO_distance_to_geo (double beginpointLat, double beginpointLon, 
                         double mag, double dir,
                         double *endpointLat, double *endpointLon)
{
   const long double A     = 6378137.0;      /* semi-major axis of ellipsoid */
   const long double RECF  = 298.257223563;  /* reciprocal of flattening (1/F) */

   /* intermediate values used in the calculation */
   long double B,C2SIGM, COSAZ1, COSSAZ, COSSIG, COSU1,
                                  DENOM, DLAM, DLAMS,FL, FIRST,RNUMER,
                                  SIG, SIG1, SINAZ, SINAZ1, SINSIG, SINU1,
                                  TA, TANU1, TB, TC, TEMP,US;

   /* convert to radians */
   beginpointLat = DEG_TO_RAD(beginpointLat);
   beginpointLon = DEG_TO_RAD(beginpointLon);
   dir = DEG_TO_RAD(dir);
   
   if( (fabs(mag) < 1e-100) )
   {
      *endpointLat = RAD_TO_DEG(beginpointLat);
      *endpointLon = RAD_TO_DEG(beginpointLon);
      return SUCCESS;
   }

   /* convert range from feet to meter */
// mag = mag/FT_per_METER;

   /* check the limits */
   if ( (fabs(beginpointLat) > PI/2.0) )
   {
      *endpointLat = 0.0;
      *endpointLon = 0.0;
      return FAILURE;
   }
   if ( (fabs(beginpointLon) > PI*2.0) )
   {
      *endpointLat = 0.0;
      *endpointLon = 0.0;
      return FAILURE;
   }
   if ( (fabs(dir) > 4.0*PI) )
   {
      *endpointLat = 0.0;
      *endpointLon = 0.0;
      return FAILURE;
   }
   if ( (mag < 0.0 || mag > 40.0E6) )
   {
      *endpointLat = 0.0;
      *endpointLon = 0.0;
      return FAILURE;
   }

   /* adjust the range */
   if (beginpointLon > PI) 
      beginpointLon = beginpointLon - 2.0 * PI;
   if (beginpointLon < -PI) 
      beginpointLon = beginpointLon + 2.0 * PI;
   if (dir < 0.0) 
         dir = dir + 4.0 * PI;
   if (dir > 2.0 * PI) 
         dir = dir - 2.0 * PI;

   /* SEMI MAJOR AXIS - B */
   B = A*( RECF -1.0 )/RECF;

   /* FLATTENING (F) */
   FL = 1.0/RECF;

   /* SINE & COSINE OF FORWARD AZIMUTH */
   SINAZ1 = sin( dir );
   COSAZ1 = cos( dir );

   /* TANGENT OF REDUCED LATITUDE (U) OF POINT 1 */
   TANU1 = ( 1.0 -FL )*sin( beginpointLat )/cos( beginpointLat );

   /* SIGMA1 (SIGMA-SUB-1) */
   SIG1 = atan2( TANU1, COSAZ1 );

   /* COSINE & SINE OF U1 FROM TRIG IDENTITIES */
   COSU1 = 1.0/sqrt( 1.0 + pow(TANU1,2) );
   SINU1 = TANU1*COSU1;

   /* SINE OF AZIMUTH (AZ) OF GEODETIC AT EQUATOR */
   SINAZ = COSU1*SINAZ1;

   /* COSINE SQUARED OF AZ USING TRIG IDENTITY */
   COSSAZ = 1.0 - pow(SINAZ,2);

   /* SMALL u SQUARED (US) */
   US = COSSAZ*( pow(A,2) - pow(B,2) )/pow(B,2);

   /* TERM A */
   TA = 1.0 +US*(4096.0 + US *( -768.0
                        +US*(320.0 -175.0 * US)))/16384.0;

   /* TERM B */
   TB = US*( 256.0 +US*( -128.0 +US*( 74.0 -47.0*US ) ) )/1024.0;

   /* FIRST TERM * FIRST ESTIMATE OF SIGMA (SIG) */
   FIRST = mag/( B*TA );
   SIG = FIRST;

   do{
         /* COSINE OF 2 SIGMA-SUB-M */
         C2SIGM = cos( 2.0*SIG1 +SIG );

         /* SINE & COSINE OF SIGMA */
         SINSIG = sin( SIG );
         COSSIG = cos( SIG );

         /* SAVE PREVIOUS SIGMA FOR COMPARISON WITH NEWLY COMPUTED SIGMA */
         TEMP = SIG;

         /* NEWEST SIGMA */
         SIG = FIRST +TB*SINSIG*( C2SIGM
                  +TB*( COSSIG*( -1.0 +2.0*pow(C2SIGM,2))
                  -TB*C2SIGM*(   -3.0 +4.0*pow(SINSIG,2))*(
                  -3.0 +4.0*pow(C2SIGM,2))/6.0 )/4.0  );

   }while (fabs(SIG-TEMP) > 1.0E-11);

   /* LATITUDE OF POINT 2 */
   /* DENOMINATOR IN 2 PARTS (TEMP ALSO USED LATER) */
   TEMP = SINU1*SINSIG -COSU1*COSSIG*COSAZ1;
   DENOM = ( 1.0 -FL )*sqrt( pow(SINAZ,2) + pow(TEMP,2) );

   /* NUMERATOR */
   RNUMER = SINU1*COSSIG +COSU1*SINSIG*COSAZ1;

   /* LATITUDE */
   *endpointLat = atan2( RNUMER, DENOM );

   /* DIFFERENCE IN LONGITUDE ON AUXILARY SPHERE (DLAMS ) */
   RNUMER = SINSIG*SINAZ1;
   DENOM = COSU1*COSSIG -SINU1*SINSIG*COSAZ1;
   DLAMS = atan2( RNUMER, DENOM );

   /* TERM C */
   TC = FL*COSSAZ*( 4.0 +FL*( 4.0 -3.0*COSSAZ ) )/16.0;

   /* DIFFERENCE IN LONGITUDE */
   DLAM = DLAMS -( 1.0 -TC )*FL*SINAZ*( SIG
               +TC*SINSIG*( C2SIGM +TC*COSSIG*( -1.0
               +2.0*pow(C2SIGM,2) ) ) );

   /* LONGITUDE OF POINT 2 */
   *endpointLon = beginpointLon +DLAM;
   if( *endpointLon >  PI ) 
         *endpointLon = *endpointLon -2.0*PI;
   if( *endpointLon < -PI ) 
         *endpointLon = *endpointLon +2.0*PI;

   *endpointLat = RAD_TO_DEG(*endpointLat);
   *endpointLon = RAD_TO_DEG(*endpointLon);
   
   return SUCCESS;
}


// end of GEO_distance_to_geo

// *****************************************************************

// This function projects a lat/long into Gnomonic projection space
// This azimuthal projection has the very interest property that
// great circle lines are straight lines
// NOTE: 1. This function is only valid for less than half a hemisphere
//          In other words (lat, lon) must be less than 90 degrees from
//          (centerlat, centerlon)


boolean_t GEO_gnomonic_geo_to_xy(
      double lat, double lon,                // geo to convert (degrees)
      double center_lat, double center_lon,  // center of projection (degrees)
      double *x, double *y)                  // OUT -- xy in projection
{
   double kp;
   double sinlat, sinlon, coslat, coslon;
   double sinclat, sinclon, cosclat,cosclon;
   double rlat, rlon, rclat, rclon;
   double deltalon;
   double R;

   // nominal Earth radius
   R = WGS84_R2_METERS;   

   rlat = DEG_TO_RAD(lat);
   rlon = DEG_TO_RAD(lon);
   rclat = DEG_TO_RAD(center_lat);
   rclon = DEG_TO_RAD(center_lon);
   deltalon = rlon - rclon;
   sinlat = sin(rlat);
   coslat = cos(rlat);
   sinlon = sin(rlon);
   coslon = cos(rlon);
   sinclat = sin(rclat);
   cosclat = cos(rclat);
   sinclon = sin(rclon);
   cosclon = cos(rclon);

   kp = (sinclat * sinlat) + (cosclat * coslat * cos(deltalon));
   kp = 1.0 / kp;

   *x = R * kp * coslat * sin(deltalon);
   *y = R * kp * ((cosclat * sinlat) - (sinclat * coslat * cos(deltalon)));
   return TRUE;
}
// end of GEO_gnomonic_geo_to_xy

// *****************************************************************

// This function converts an xy pair into a lat/long in 
// Gnomonic projection space.  This azimuthal projection has the very 
// interest property that great circle lines are straight lines
// NOTE: 1. This function is only valid for less than half a hemisphere
//          In other words (lat, lon) must be less than 90 degrees from
//          (centerlat, centerlon)

boolean_t GEO_gnomonic_xy_to_geo(
      double x, double y,                    // xy to convert
      double center_lat, double center_lon,  // center of projection (degrees)
      double *lat, double *lon)              // OUT -- geo in projection (degrees)
{
   double c, p;
   double tf;
   double sinclat, sinclon, cosclat,cosclon;
   double rlat, rlon, rclat, rclon;
   double sinc, cosc, cosrcp;
   double R;

   // check inputs
   if ((center_lat < -90.0) || (center_lat > 90.0))
      return FALSE;

   if ((center_lon < -180.0) || (center_lon > 180.0))
      return FALSE;

   // nominal Earth radius
   R = WGS84_R2_METERS;

   rclat = DEG_TO_RAD(center_lat);
   rclon = DEG_TO_RAD(center_lon);
   sinclat = sin(rclat);
   cosclat = cos(rclat);
   sinclon = sin(rclon);
   cosclon = cos(rclon);

   p = sqrt((x * x) + (y * y));
   c = atan(p / R);
   cosrcp = rclat / p;
   cosrcp = cos(cosrcp);
   cosc = cos(c);
   sinc = sin(c);
   rlat = (cosc * sinclat) + ((y * sinc * cosclat) / p);
   rlat = asin( rlat );
   *lat = RAD_TO_DEG(rlat);

   if (center_lat == 90.0)
   {
      if (y == 0.0)
         *lon = 0.0;
      else
      {
         rlon = rclon + atan(x / -y);
         *lon = RAD_TO_DEG(rlon);
      }
      return TRUE;
   }

   if (center_lat == -90.0)
   {
      if (y == 0.0)
         *lon = 0.0;
      else
      {
         rlon = rclon + atan(x / y);
         *lon = RAD_TO_DEG(rlon);
      }
      return TRUE;
   }
      
   // otherwise
   
   tf = (p * cosclat * cosc) - (y * sinclat * sinc);
   rlon = x * sinc;
   rlon /= tf;
   rlon = atan(rlon);
   rlon += rclon;
   *lon = RAD_TO_DEG(rlon);
   return TRUE;
}
// end of GEO_gnomonic_xy_to_geo

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

int GEO_xy_line_intersection(
                        double ax, double ay,      //line 1 start point
                        double bx, double by,      //line 1 end point
                        double cx, double cy,      //line 2 start point
                        double dx, double dy,      //line 2 end point
                        double *ix, double *iy)    // intersection point
{
   double r, s, t;

   *ix = 0.0;
   *iy = 0.0;
   r = ((ay - cy) * (dx - cx)) - ((ax - cx) * (dy - cy));
   t = ((bx - ax) * (dy - cy)) - ((by - ay) * (dx - cx));
   if (t == 0.0)
      return INTERSECT_RSLT_PARALLEL;

   r /= t;

   s = ((ay - cy) * (bx - ax)) - ((ax - cx) * (by - ay));
   s /= t;

   *ix = ax + (r * (bx - ax));
   *iy = ay + (r * (by - ay));

   if ((r >= 0.0) && (r <= 1.0) && (s >= 0.0) && (s <= 1.0))
      return INTERSECT_RSLT_YES;
   else
      return INTERSECT_RSLT_NO_SEGMENT;  // "lines" intersect, but not segments
}
// end of GEO_xy_line_intersection

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

boolean_t GEO_great_circle_intersection(
      double lat1, double lon1,     // starting point of first line
      double lat2, double lon2,     // ending point of first line
      double lat3, double lon3,     // starting point of second line
      double lat4, double lon4,     // ending point of second line
      double *ilat, double *ilon)   // intersection of two geo lines
{
   double x1, y1, x2, y2, x3, y3, x4, y4, ix, iy;
   double tlat, tlon;
   int rslt;

   rslt = GEO_gnomonic_geo_to_xy(lat1, lon1, lat1, lon1, &x1, &y1);
   if (!rslt)
      return FALSE;
   rslt = GEO_gnomonic_geo_to_xy(lat2, lon2, lat1, lon1, &x2, &y2);
   if (!rslt)
      return FALSE;
   rslt = GEO_gnomonic_geo_to_xy(lat3, lon3, lat1, lon1, &x3, &y3);
   if (!rslt)
      return FALSE;
   rslt = GEO_gnomonic_geo_to_xy(lat4, lon4, lat1, lon1, &x4, &y4);
   if (!rslt)
      return FALSE;
   rslt = GEO_xy_line_intersection(x1, y1, x2, y2, x3, y3, x4, y4, &ix, &iy);
   if (rslt != INTERSECT_RSLT_YES)
      return FALSE;
   rslt = GEO_gnomonic_xy_to_geo(ix, iy, lat1, lon1, &tlat, &tlon);
   if (!rslt)
      return FALSE;
   *ilat = tlat;
   *ilon = tlon;
   return TRUE;
}
// end of GEO_great_circle_intersection

// *****************************************************************
// ******************************************************************

// function to convert geo in decimal degrees to Cartesian xyz in meters
int GEO_geo_to_xyz(double lat, double lon, double height, 
               double *x, double *y, double *z)
{
   double rlat, rlon;
// double n, f, e, a, b;
   double n, a, b;
   double sinlat, coslat, sinlon, coslon;

   rlat = DEG_TO_RAD(lat);
   rlon = DEG_TO_RAD(lon);

   sinlat = sin(rlat);
   coslat = cos(rlat);
   sinlon = sin(rlon);
   coslon = cos(rlon);

   a = 6378137.0; // WGS84 meters at equator
// f = 1.0 / 298.257223563;  // flattening
// e = (2.0 * f) - (f * f);  // eccentricity squared
// n = a / sqrt(1.0 - (e * sinlat * sinlat));

   b = 6356752.3142;
   n = (a*a) / sqrt((a*a*coslat*coslat) + (b*b*sinlat*sinlat));

   *x = (n + height) * coslat * coslon;
   *y = (n + height) * coslat * sinlon;
// *z = ((n * (1.0 - e)) + height) * sinlat;
   *z = ((n * (b*b)/(a*a)) + height) * sinlat;

   return SUCCESS;
}
// end of GEO_geo_to_xyz

// ******************************************************************
// ******************************************************************

// function to convert Cartesian xyz in meters to decimal degrees
int GEO_xyz_to_geo(double x, double y, double z, 
               double *lat, double *lon, double *height)
{
   double tlat, tlon, tf, tf2, tf3;
   double f, e, a, u, p, r;
   double sinu, cosu;

   double sinlat, coslat;

   a = 6378137.0; // WGS84 meters at equator
   f = 1.0 / 298.257223563;  // flattening
   e = (2.0 * f) - (f * f);  // eccentricity squared
   p = sqrt((x * x) + (y * y));
   r = sqrt((p * p) + (z * z));
   u = (z / p) * ((1.0 - f) + (e * (a / r)));
   u = atan(u);
   sinu = sin(u);
   cosu = cos(u);

   tf = (z * (1.0 -f)) + (e * a * sinu * sinu * sinu);
   tf2 = (1.0 - f) * (p - (e * a * cosu * cosu * cosu));
   tf3 = tf / tf2;
   tf3 = atan(tf3);

   tlat = tf3;
   tlon = atan(y / x);

   coslat = cos(tlat);
   sinlat = sin(tlat);
   tf = (p * coslat) + (z * sinlat) - (a * sqrt(1.0 - (e * sinlat * sinlat)));
   *height = tf;

   *lat = RAD_TO_DEG(tlat);
   *lon = RAD_TO_DEG(tlon);

   // correct lon if needed
   if (x < 0)
      *lon += 180.0;
   if (*lon > 180.0)
      *lon -= 360.0;

   return SUCCESS;
}
// end of GEO_xyz_to_geo

// ******************************************************************
// *****************************************************************

// this function takes an array of geo coords and computes the "center of mass"
int GEO_center_of_mass(d_geo_t *geo, int numpts, d_geo_t *center_geo)
{
   double sx, sy, sz;
   double tx, ty, tz;
   double tlat, tlon, th;
   int k, rslt;

   sx = 0;
   sy = 0;
   sz = 0;

   for (k=0; k<numpts; k++)
   {
      rslt = GEO_geo_to_xyz(geo[k].lat, geo[k].lon, 0.0, &tx, &ty, &tz);
      if (rslt != SUCCESS)
         return FAILURE;
      sx += tx;
      sy += ty;
      sz += tz;
   }

   tx = sx / (double) numpts;
   ty = sy / (double) numpts;
   tz = sz / (double) numpts;

   rslt = GEO_xyz_to_geo(tx, ty, tz, &tlat, &tlon, &th);
   if (rslt != SUCCESS)
      return FAILURE;
   
   center_geo->lat = tlat;
   center_geo->lon = tlon;

   return SUCCESS;
}

// ******************************************************************
// *****************************************************************

// test code
#if 0

main()
{
   double lat1, lat2, lng1, lng2, dist, az;

   lat1 = -34.0;
   lat2 = -35.0;
   lng1 = -84.0;
   lng2 = -85.0;
   printf("Lat2 = %9.5f,  Lng2 = %10.5f\n", lat2, lng2);
   GEO_geo_to_distance(lat1, lng1, lat2, lng2, &dist, &az);
   printf("Dist= %9.4f,  Az= %3.2f\n", dist, az);
   GEO_distance_to_geo(lat1, lng1, dist, az, &lat2, &lng2);
   printf("Lat2 = %9.5f,  Lng2 = %10.5f\n", lat2, lng2);

   return 0;
}

#endif
